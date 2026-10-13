#define WIN32_LEAN_AND_MEAN
#include "RR/Patching/Patches.h"
#include "RR/BuildGuard.h"
#include "Utils/Il2CppDump.h"
#include "Utils/Logging.h"

// DllMain runs with the Windows loader lock held. File/configuration work,
// fingerprint hashing, DNS resolution and hook installation belong on this
// worker, which starts after DLL_PROCESS_ATTACH returns. Never wait for it in
// DllMain: that would block the loader notifications needed to start the thread.
static DWORD WINAPI InitializePatch(void* parameter) {
    const auto module = static_cast<HMODULE>(parameter);
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        // A usable log path does not exist yet; report this bootstrap failure
        // through the debugger rather than writing relative to an unknown CWD.
        OutputDebugStringW(L"[Patch2026] Cannot resolve the patch DLL directory.\n");
        return 7;
    }
    const std::wstring modulePath(path, length);
    const auto separator = modulePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return 7;
    }
    const auto directory = modulePath.substr(0, separator);
    PatchLogging::BeginSession(directory + L"\\2026patch.log");

    // Configuration precedes all hook creation, so the first intercepted request
    // sees the same settings as injector preflight. Blank routing fields retain
    // client endpoints; malformed supplied values abort this worker with no hooks.
    RR::Config::Settings settings;
    std::string error;
    if (!RR::Config::Load(directory + L"\\2026patch.ini", settings, error)) {
        PatchLogging::Log("[Config] %s", error.c_str());
        return 7;
    }
    PatchLogging::verboseLogging = settings.verbose;
    PatchLogging::Log("[Build] Native 20260323.14 patch");

    // Injector preflight checks files in its target installation. Repeat the
    // check against the modules loaded by THIS process before resolving any RVA:
    // a manual DLL load must not bypass the version boundary.
    if (!RR::BuildGuard::SupportsLoadedClient()) {
        PatchLogging::Log("[Build] fingerprint mismatch; no hooks installed");
        return 1;
    }

    if (settings.dumpIl2Cpp || settings.dumpMetadata || settings.dumpMethods) {
        // Independent of the hooks: waits for IL2CPP on its own thread and writes beside the DLL.
        Il2CppDump::Start(directory, settings.dumpIl2Cpp, settings.dumpMetadata, settings.dumpMethods);
    }

    // File diagnostics require no console. AllocConsole can steal Unity's focus
    // during loading; this entry point does not
    // create a window or attempt a timing-dependent foreground handback.
    // Initialize owns runtime resolution and queued hook installation. Successful
    // DLL loading alone does not prove hook readiness; failures appear in the log.
    const DWORD status = RR::Patches::Initialize(settings);
    if (status != 0) {
        PatchLogging::Log("[Build] initialization failed status=%lu", status);
    }
    return status;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(instance);
        const HANDLE worker = CreateThread(nullptr, 0, InitializePatch, instance, 0, nullptr);
        if (!worker) {
            return FALSE;
        }
        // Closing our handle does not stop the worker. Its status is asynchronous;
        // retaining or joining the handle here would not establish hook readiness.
        CloseHandle(worker);
        break;
    }
    case DLL_PROCESS_DETACH:
        // This injected DLL is process-lifetime: live unloading is unsupported.
        // Do not join workers, unhook or shut down GDI+ under the loader lock,
        // especially during process termination when other threads are gone.
        break;
    }
    return TRUE;
}
