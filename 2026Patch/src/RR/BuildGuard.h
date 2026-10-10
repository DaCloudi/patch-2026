#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace RR::BuildGuard {
    struct ModuleFingerprint {
        const wchar_t* name;
        const char* hash;
    };

    // The loaded DLL and injector use the same exact three-module boundary.
    // Version labels alone cannot authorize RVAs; even one changed module must
    // fail closed. Adding a client requires re-deriving addresses and layouts.
    inline constexpr ModuleFingerprint SupportedModules[] = {
        {L"GameAssembly.dll", "A6D90770CDFDA4043EADB278A16198ED0907C3BA2AC09BC37D06CD4DD22A1D84"},
        {L"Referee.dll", "5868C90A7C67757F3B678AA356CBD933D646C9749E0ACAA31B7DCC9EAEE12EAD"},
        {L"UnityPlayer.dll", "30F4A99C49566B2C5045469B5A39572DD2298ADC31F7FE2BC39D8BB8FC9D75BB"}};

    // Hash the on-disk module before using fingerprint-specific runtime addresses.
    inline bool MatchesPath(const wchar_t* path, const char* expected) {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
        if (ok) {
            ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
        }
        unsigned char buffer[65536];
        DWORD count = 0;
        while (ok) {
            if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) {
                ok = false;
                break;
            }
            if (!count) {
                break;
            }
            ok = BCryptHashData(hash, buffer, count, 0) >= 0;
        }
        unsigned char digest[32] = {};
        if (ok) {
            ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
        }
        if (hash) {
            BCryptDestroyHash(hash);
        }
        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        CloseHandle(file);
        char actual[65] = {};
        constexpr char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < sizeof(digest); ++i) {
            actual[2 * i] = hex[digest[i] >> 4];
            actual[2 * i + 1] = hex[digest[i] & 15];
        }
        return ok && strcmp(actual, expected) == 0;
    }

    inline bool MatchesFile(const wchar_t* moduleName, const char* expected) {
        wchar_t path[MAX_PATH] = {};
        HMODULE module = GetModuleHandleW(moduleName);
        DWORD pathLength = module ? GetModuleFileNameW(module, path, MAX_PATH) : 0;
        return pathLength && pathLength < MAX_PATH && MatchesPath(path, expected);
    }

    // Resolve each loaded module's on-disk path rather than trusting the process
    // working directory. This checks file identity, not a hash of relocated code
    // pages; hook-specific runtime invariants still need their own checks.
    inline bool SupportsLoadedClient() {
        for (const auto& module : SupportedModules) {
            if (!MatchesFile(module.name, module.hash)) {
                return false;
            }
        }
        return true;
    }

    // Read-only preflight: hash files without loading game DLLs into the injector.
    // The DLL repeats verification after load; this check is not hook readiness.
    inline bool SupportsInstallation(const std::wstring& root) {
        for (const auto& module : SupportedModules) {
            const auto path = root + L"\\" + module.name;
            if (!MatchesPath(path.c_str(), module.hash)) {
                return false;
            }
        }
        return true;
    }
}
