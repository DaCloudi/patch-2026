#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <cwctype>
#include "../2026Patch/src/RR/BuildGuard.h"
#include "../2026Patch/src/RR/Methods.h"
#include "../2026Patch/src/RR/Config.h"

// 20260323.14 / A6D90770... GameAssembly + 5868C90A... Referee.
// Runtime probe: caller 0x217EC62 loads [Referee+0x6C10858], then its
// pointee, and adds 0x100040 before the fault at 0x69839D (mov eax,[r8]).
// The stock service cannot initialize this arena after backend retirement.
// Supplying a local 64 MiB arena is the standalone native bootstrap seam
// used by the March attempt, re-derived here for the target fingerprint.
static uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    MODULEENTRY32W mod{};
    mod.dwSize = sizeof(mod);
    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &mod)) {
        do {
            if (!_wcsicmp(mod.szModule, name)) {
                base = reinterpret_cast<uintptr_t>(mod.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &mod));
    }
    CloseHandle(snapshot);
    return base;
}

static std::wstring ReadyName(const std::wstring& root) {
    // Launchers wait for a named event tied to this configured install path.
    // Case folding follows the process-path comparison below. This is a readiness
    // identifier, not an authentication token or a filesystem identity check.
    unsigned long long hash = 14695981039346656037ull;
    for (wchar_t c : root) {
        hash ^= static_cast<unsigned long long>(towlower(c));
        hash *= 1099511628211ull;
    }
    return L"Local\\Patch2026InjectorReady-" + std::to_wstring(hash);
}

int wmain(int argc, wchar_t** argv) {
    if ((argc == 2 || argc == 3) && !wcscmp(argv[1], L"--wait-ready")) {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
            return 2;
        }
        std::wstring root(path);
        root = root.substr(0, root.find_last_of(L"\\"));
        if (argc == 3) {
            root = argv[2];
        }
        const auto name = ReadyName(root);
        const auto deadline = GetTickCount64() + 30000;
        while (GetTickCount64() < deadline) {
            HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
            if (ready) {
                const auto result = WaitForSingleObject(ready, 100);
                CloseHandle(ready);
                if (result == WAIT_OBJECT_0) {
                    return 0;
                }
            }
            Sleep(20);
        }
        puts("[-] Injector did not become ready. Check its window.");
        return 2;
    }
    // Drop-in mode anchors the DLL/INI to this executable's directory. Explicit
    // paths support source-tree testing. Check modes stop before opening another
    // process or modifying its memory, so launchers can fail preflight safely.
    const bool dropIn = argc == 1 || (argc == 2 && !wcscmp(argv[1], L"--check-install"));
    const bool checkOnly = (dropIn && argc == 2) || (argc == 4 && !wcscmp(argv[3], L"--check-config"));
    if (!dropIn && (argc < 2 || argc > 4 || (argc == 4 && !checkOnly))) {
        puts("Usage: Injector.exe [client-folder [patch-dll [--check-config]]]");
        return 2;
    }
    puts("Rec Room 2026 patch injector\n----------------------------");
    SetConsoleTitleW(L"Injector");
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::wstring root, dll;
    if (dropIn) {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            puts("[-] Injector path is too long.");
            return 2;
        }
        root = path;
        root = root.substr(0, root.find_last_of(L"\\"));
        dll = root + L"\\2026patch.dll";
    } else {
        root = argv[1];
        if (argc >= 3) {
            wchar_t full[MAX_PATH]{};
            DWORD length = GetFullPathNameW(argv[2], MAX_PATH, full, nullptr);
            if (!length || length >= MAX_PATH) {
                puts("[-] Patch DLL path is invalid.");
                return 2;
            }
            dll = full;
        }
    }
    if (!dll.empty() && GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        puts("2026patch.dll is missing. Extract all files from the patch ZIP.");
        return 2;
    }
    if (!dll.empty()) {
        const auto folder = dll.substr(0, dll.find_last_of(L"\\/"));
        RR::Config::Settings settings;
        std::string error;
        if (!RR::Config::Load(folder + L"\\2026patch.ini", settings, error)) {
            printf("[-] %s\n", error.c_str());
            return 2;
        }
    }
    const bool injectPatch = !dll.empty();
    if (!RR::BuildGuard::SupportsInstallation(root)) {
        puts("This patch requires Rec Room 20260323.14. Extract it into that version's install folder.");
        return 2;
    }
    if (checkOnly) {
        puts("20260323.14 installation and patch files verified.");
        return 0;
    }
    printf("[*] Patch DLL: %ls\n", dll.c_str());
    puts("[*] Waiting for RecRoom.exe. If launching manually, start the game now...");
    fflush(stdout);
    // Serialize attach attempts; do not launch or terminate the user's client.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Patch2026Injector");
    if (!mutex) {
        return 3;
    }
    const DWORD lockResult = WaitForSingleObject(mutex, INFINITE);
    if (lockResult != WAIT_OBJECT_0 && lockResult != WAIT_ABANDONED) {
        CloseHandle(mutex);
        return 3;
    }
    // Signal only after validation and attach serialization. "Ready" means this
    // injector is about to watch for the client, not that the DLL's asynchronous
    // initialization has completed. Start the game after this handshake so the
    // early Referee arena seam can be reached before its first dereference.
    HANDLE ready = CreateEventW(nullptr, TRUE, TRUE, ReadyName(root).c_str());
    if (!ready) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }
    SetEvent(ready);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PROCESS_INFORMATION process{};
    // Match the full executable path, not just the name: multiple installs may
    // coexist. A different configured path can leave this loop waiting forever.
    // Already-loaded current/legacy patches are skipped to prevent double hooks.
    const std::wstring expected = root + L"\\RecRoom.exe";
    while (!process.hProcess) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"RecRoom.exe")) {
                    continue;
                }
                HANDLE candidate =
                    OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                                    PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | SYNCHRONIZE,
                                FALSE, entry.th32ProcessID);
                if (!candidate) {
                    continue;
                }
                wchar_t path[MAX_PATH]{};
                DWORD length = MAX_PATH;
                if (QueryFullProcessImageNameW(candidate, 0, path, &length) &&
                    !_wcsicmp(path, expected.c_str()) && !ModuleBase(entry.th32ProcessID, L"2026patch.dll") &&
                    !ModuleBase(entry.th32ProcessID, L"ProotPatch2026.dll")) {
                    process.hProcess = candidate;
                    process.dwProcessId = entry.th32ProcessID;
                    break;
                }
                CloseHandle(candidate);
            } while (Process32NextW(snap, &entry));
        }
        if (snap != INVALID_HANDLE_VALUE) {
            CloseHandle(snap);
        }
        if (!process.hProcess) {
            Sleep(1);
        }
    }
    printf("[+] Found unpatched game, PID %lu\n", process.dwProcessId);
    puts("[*] Preparing the client for patch loading...");
    bool initialized = false;
    bool injected = !injectPatch;
    uintptr_t referee = 0;
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
        if (!referee) {
            referee = ModuleBase(process.dwProcessId, L"Referee.dll");
        }
        if (!referee) {
            SwitchToThread();
            continue;
        }
        // Fingerprints authorize this build; the runtime instruction and pointer
        // checks authorize the bootstrap moment. Wait for the measured mov eax,
        // [r8] instruction and initialized pointer slot rather than writing blindly
        // as soon as a module name appears. Existing nonzero arenas are retained.
        unsigned char instruction[3]{};
        SIZE_T count = 0;
        if (!ReadProcessMemory(process.hProcess,
                               reinterpret_cast<void*>(referee + RR::Methods::Referee::BootstrapInstruction),
                               instruction, 3, &count) ||
            count != 3 || memcmp(instruction, "\x41\x8B\x00", 3)) {
            SwitchToThread();
            continue;
        }
        uintptr_t pointer = 0, arena = 0;
        if (!ReadProcessMemory(process.hProcess,
                               reinterpret_cast<void*>(referee + RR::Methods::Referee::ArenaSlot), &pointer,
                               sizeof(pointer), &count) ||
            count != sizeof(pointer) || !pointer) {
            SwitchToThread();
            continue;
        }
        if (!ReadProcessMemory(process.hProcess, reinterpret_cast<void*>(pointer), &arena, sizeof(arena),
                               &count) ||
            count != sizeof(arena)) {
            SwitchToThread();
            continue;
        }
        if (!arena) {
            arena = reinterpret_cast<uintptr_t>(VirtualAllocEx(process.hProcess, nullptr, 0x04000000,
                                                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!arena) {
                break;
            }
            // This build creates three channel descriptors at stride 0x1500030.
            // Each consumes two length-prefixed double-buffered queues. A zero
            // length underflows (length - 0x1c) / 2 in Referee+0x698390, producing
            // invalid second-buffer pointers and later access violations.
            // Use bounded local queues within the allocated 64 MiB arena. There
            // is no service consumer; normal full-queue handling remains intact.
            bool queuesReady = true;
            for (unsigned channel = 0; channel < 3 && queuesReady; ++channel) {
                const uintptr_t segment = arena + channel * 0x1500030ull;
                const uintptr_t offsets[2] = {0xA0, 0x100040};
                const uint32_t lengths[2] = {0x80000, 0x1000000};
                for (unsigned queue = 0; queue < 2; ++queue) {
                    const uint32_t capacity = (lengths[queue] - 0x1C) / 2;
                    const uint32_t header[8] = {lengths[queue], 0, 0, capacity, 0, 0, capacity, 0};
                    if (!WriteProcessMemory(process.hProcess,
                                            reinterpret_cast<void*>(segment + offsets[queue]), header,
                                            sizeof(header), &count) ||
                        count != sizeof(header)) {
                        queuesReady = false;
                        break;
                    }
                }
            }
            if (!queuesReady ||
                !WriteProcessMemory(process.hProcess, reinterpret_cast<void*>(pointer), &arena, sizeof(arena),
                                    &count) ||
                count != sizeof(arena)) {
                break;
            }
        }
        initialized = true;
        break;
    }
    if (initialized && injectPatch) {
        puts("[*] Injecting...");
        const ULONGLONG injectDeadline = GetTickCount64() + 5000;
        while (!ModuleBase(process.dwProcessId, L"GameAssembly.dll") && GetTickCount64() < injectDeadline &&
               WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            Sleep(10);
        }
        SIZE_T size = (dll.size() + 1) * sizeof(wchar_t);
        void* path =
            VirtualAllocEx(process.hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (path && WriteProcessMemory(process.hProcess, path, dll.c_str(), size, nullptr)) {
            // LoadLibraryW may be forwarded to another system module. Find the
            // actual owning module and relocate its function offset into the
            // target process; a local absolute address is not a remote contract.
            auto loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
            HMODULE owner = nullptr;
            wchar_t ownerPath[MAX_PATH]{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<const wchar_t*>(loader), &owner);
            GetModuleFileNameW(owner, ownerPath, MAX_PATH);
            const wchar_t* ownerName = wcsrchr(ownerPath, L'\\');
            ownerName = ownerName ? ownerName + 1 : ownerPath;
            uintptr_t remoteOwner = ModuleBase(process.dwProcessId, ownerName);
            auto remoteLoad = remoteOwner ? reinterpret_cast<LPTHREAD_START_ROUTINE>(
                                                remoteOwner + reinterpret_cast<uintptr_t>(loader) -
                                                reinterpret_cast<uintptr_t>(owner))
                                          : nullptr;
            HANDLE thread =
                remoteLoad ? CreateRemoteThread(process.hProcess, nullptr, 0, remoteLoad, path, 0, nullptr)
                           : nullptr;
            if (thread) {
                if (WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0) {
                    const wchar_t* name = wcsrchr(dll.c_str(), L'\\');
                    name = name ? name + 1 : dll.c_str();
                    // Module presence confirms DLL loading only. main.cpp starts
                    // hooks asynchronously; inspect 2026patch.log for readiness.
                    injected = ModuleBase(process.dwProcessId, name) != 0;
                    VirtualFreeEx(process.hProcess, path, 0, MEM_RELEASE);
                }
                CloseHandle(thread);
            }
        }
        // The path remains valid if the remote loader is still running.
    }
    if (initialized && injected) {
        puts("[+] Done. The patch is loaded.");
    } else {
        puts("[-] Client preparation or DLL loading failed. Start the injector before the game, "
             "or use a supplied batch launcher.");
    }
    CloseHandle(process.hProcess);
    CloseHandle(ready);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    if (initialized && injected) {
        Sleep(1500);
    }
    return initialized && injected ? 0 : 1;
}
