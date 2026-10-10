#pragma once

#include <windows.h>
#include <fstream>
#include "NetworkConfig.h"

namespace RR::Config {
    // One snapshot feeds both preflight and the DLL. Runtime callbacks copy the
    // values they need; there is no live INI reload while hooks are executing.
    // Zero FPS and false flags are native-behavior defaults when options are absent.
    struct Settings {
        Network network;
        std::string tachyonHost, tachyonServerId, voiceKeyXml;
        int framesPerSecond = 0;
        bool verbose = false;
    };

    // Win32 INI reads return the fallback for an absent field. Empty routing
    // values are intentional, not a parse error. Reject truncation before UTF-8
    // conversion so a shortened hostname or RSA key cannot become an override.
    inline bool ReadIni(const std::wstring& path, const wchar_t* name, std::string& value) {
        wchar_t text[4098]{};
        char utf8[16392]{};
        const DWORD length = GetPrivateProfileStringW(L"Network", name, L"", text, 4098, path.c_str());
        if (length >= 4097 || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8,
                                                   sizeof(utf8), nullptr, nullptr)) {
            return false;
        }
        value = utf8;
        return true;
    }

    // The file itself must exist, but ServerUrl and ServerDomain can be omitted
    // independently. Validate supplied values before installing any hooks; avoid
    // silently mixing a mistyped operator endpoint with the client's fallback.
    inline bool Load(const std::wstring& path, Settings& settings, std::string& error) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = "2026patch.ini is missing. Place it beside 2026patch.dll.";
            return false;
        }
        if (!ReadIni(path, L"ServerUrl", settings.network.serverUrl) ||
            !ReadIni(path, L"ServerDomain", settings.network.serverDomain) ||
            !ReadIni(path, L"CachedServerDomain", settings.network.cachedServerDomain) ||
            !ReadIni(path, L"TachyonHost", settings.tachyonHost) ||
            !ReadIni(path, L"TachyonServerId", settings.tachyonServerId) ||
            !ReadIni(path, L"VoiceKeyXml", settings.voiceKeyXml)) {
            error = "A Network setting is too long or contains invalid text.";
            return false;
        }
        settings.network.serverDomain = Lower(settings.network.serverDomain);
        settings.network.cachedServerDomain = Lower(settings.network.cachedServerDomain);
        if (!settings.network.Valid()) {
            error =
                "When supplied, ServerUrl must be an HTTP(S) origin and ServerDomain a valid service domain "
                "covering it.";
            return false;
        }
        settings.network.disableCertificatePinning =
            GetPrivateProfileIntW(L"Network", L"DisableCertificatePinning", 0, path.c_str()) != 0;
        settings.network.allowUnsignedImages =
            GetPrivateProfileIntW(L"Network", L"AllowUnsignedImages", 0, path.c_str()) != 0;
        settings.verbose = GetPrivateProfileIntW(L"Logging", L"Verbose", 0, path.c_str()) != 0;
        settings.framesPerSecond =
            static_cast<int>(GetPrivateProfileIntW(L"ScreenShare", L"FramesPerSecond", 0, path.c_str()));
        if (settings.framesPerSecond < 0 || settings.framesPerSecond > 15) {
            error = "FramesPerSecond must be between 0 and 15 (0 retains the client presets).";
            return false;
        }
        // Developer launcher overrides are process-local and applied after the
        // INI. Both injector and client inherit them, so preflight sees exactly
        // the same endpoint/key as the DLL. A public-key file path is never logged.
        char endpoint[320]{};
        const DWORD endpointLength =
            GetEnvironmentVariableA("PATCH26_TACHYON_ENDPOINT", endpoint, sizeof(endpoint));
        if (endpointLength >= sizeof(endpoint)) {
            error = "Tachyon endpoint override is too long.";
            return false;
        }
        if (endpointLength) {
            settings.tachyonHost = endpoint;
        }
        wchar_t keyPath[32768]{};
        const DWORD keyLength = GetEnvironmentVariableW(L"PATCH26_TACHYON_PUBLIC_KEY", keyPath, 32768);
        if (keyLength >= 32768) {
            error = "Public key path is too long.";
            return false;
        }
        if (keyLength) {
            std::ifstream input(keyPath, std::ios::binary);
            char key[4097]{};
            if (!input) {
                error = "Cannot open the Tachyon public key file.";
                return false;
            }
            input.read(key, sizeof(key));
            settings.voiceKeyXml.assign(key, static_cast<size_t>(input.gcount()));
            if (settings.voiceKeyXml.empty()) {
                error = "The public key file is empty.";
                return false;
            }
        }
        // A remote endpoint must explicitly pair with the operator's public key;
        // only loopback may reuse the client's key. Private RSA material belongs
        // on the server and is rejected even if Tachyon is otherwise unconfigured.
        if (!settings.voiceKeyXml.empty() && !PublicKey(settings.voiceKeyXml)) {
            error = "VoiceKeyXml must contain only public RSA Modulus and Exponent fields.";
            return false;
        }
        if (!settings.tachyonHost.empty()) {
            std::string host, port;
            if (!Endpoint(settings.tachyonHost, host, port)) {
                error = "TachyonHost must be a hostname or IPv4 address followed by a port from 1 to 65535.";
                return false;
            }
            if (host != "127.0.0.1" && host != "localhost" && settings.voiceKeyXml.empty()) {
                error = "A remote TachyonHost requires VoiceKeyXml or a public key file override.";
                return false;
            }
        } else if (!settings.voiceKeyXml.empty() || !settings.tachyonServerId.empty()) {
            error = "TachyonHost is required when configuring a Tachyon key or server ID.";
            return false;
        }
        return true;
    }
}
