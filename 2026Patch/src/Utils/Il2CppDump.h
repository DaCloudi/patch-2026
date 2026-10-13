#pragma once

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include "../RR/Methods.h"
#include "Logging.h"

// Opt-in diagnostic (DumpIl2Cpp=true): lists the running client's managed classes, fields and
// methods with their native addresses. The shipped global-metadata.dat is a placeholder (every byte
// 0x52) and the real metadata only exists in memory, so offline dumpers cannot read this build.
// The output holds names, types, offsets and RVAs only. No strings, accounts or tokens are read.
namespace Il2CppDump {
    // il2cpp_* exports resolved by name from GameAssembly.dll. The raw Unity API table is not used:
    // its slot order is only known for the few slots the patch already depends on.
    struct Api {
        void* (*domainGet)() = nullptr;
        void** (*domainGetAssemblies)(void*, size_t*) = nullptr;
        void* (*threadAttach)(void*) = nullptr;
        void* (*assemblyGetImage)(void*) = nullptr;
        const char* (*imageGetName)(void*) = nullptr;
        size_t (*imageGetClassCount)(void*) = nullptr;
        void* (*imageGetClass)(void*, size_t) = nullptr;
        const char* (*className)(void*) = nullptr;
        const char* (*classNamespace)(void*) = nullptr;
        void* (*classParent)(void*) = nullptr;
        void* (*classFields)(void*, void**) = nullptr;
        void* (*classMethods)(void*, void**) = nullptr;
        const char* (*fieldName)(void*) = nullptr;
        void* (*fieldType)(void*) = nullptr;
        size_t (*fieldOffset)(void*) = nullptr;
        int (*fieldFlags)(void*) = nullptr;
        char* (*typeName)(void*) = nullptr;
        const char* (*methodName)(void*) = nullptr;
        uint32_t (*methodParamCount)(void*) = nullptr;
        void* (*methodReturnType)(void*) = nullptr;
        void* (*methodParam)(void*, uint32_t) = nullptr;
        uint32_t (*methodFlags)(void*, uint32_t*) = nullptr;
        void (*freeMemory)(void*) = nullptr;
    };

    inline bool Resolve(HMODULE game, Api& api) {
        bool ok = true;
        auto load = [&](auto& slot, const char* name) {
            slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(GetProcAddress(game, name));
            if (!slot) {
                PatchLogging::Log("[Dump] GameAssembly does not export %s", name);
                ok = false;
            }
        };
        load(api.domainGet, "il2cpp_domain_get");
        load(api.domainGetAssemblies, "il2cpp_domain_get_assemblies");
        load(api.threadAttach, "il2cpp_thread_attach");
        load(api.assemblyGetImage, "il2cpp_assembly_get_image");
        load(api.imageGetName, "il2cpp_image_get_name");
        load(api.imageGetClassCount, "il2cpp_image_get_class_count");
        load(api.imageGetClass, "il2cpp_image_get_class");
        load(api.className, "il2cpp_class_get_name");
        load(api.classNamespace, "il2cpp_class_get_namespace");
        load(api.classParent, "il2cpp_class_get_parent");
        load(api.classFields, "il2cpp_class_get_fields");
        load(api.classMethods, "il2cpp_class_get_methods");
        load(api.fieldName, "il2cpp_field_get_name");
        load(api.fieldType, "il2cpp_field_get_type");
        load(api.fieldOffset, "il2cpp_field_get_offset");
        load(api.fieldFlags, "il2cpp_field_get_flags");
        load(api.typeName, "il2cpp_type_get_name");
        load(api.methodName, "il2cpp_method_get_name");
        load(api.methodParamCount, "il2cpp_method_get_param_count");
        load(api.methodReturnType, "il2cpp_method_get_return_type");
        load(api.methodParam, "il2cpp_method_get_param");
        load(api.methodFlags, "il2cpp_method_get_flags");
        load(api.freeMemory, "il2cpp_free");
        return ok;
    }

    struct Image {
        uintptr_t base = 0;
        uintptr_t end = 0;
    };

    // Type names are allocated by IL2CPP and must be released with il2cpp_free.
    inline void WriteType(const Api& api, FILE* out, void* type) {
        char* name = type ? api.typeName(type) : nullptr;
        fputs(name ? name : "?", out);
        if (name) {
            api.freeMemory(name);
        }
    }

    // An RVA is printed only when the value really points inside GameAssembly.
    inline void WriteRva(FILE* out, const char* label, uintptr_t value, const Image& image) {
        if (value >= image.base && value < image.end) {
            fprintf(out, " %s=0x%llX", label, static_cast<unsigned long long>(value - image.base));
        }
    }

    // Contains no objects with destructors so it can sit inside an SEH __try block.
    inline void WriteClass(const Api& api, FILE* out, void* klass, const Image& image) {
        const char* ns = api.classNamespace(klass);
        const char* name = api.className(klass);
        fprintf(out, "\nclass %s%s%s", ns && *ns ? ns : "", ns && *ns ? "." : "", name ? name : "?");
        if (void* parent = api.classParent(klass)) {
            const char* parentName = api.className(parent);
            fprintf(out, " : %s", parentName ? parentName : "?");
        }
        fputc('\n', out);
        void* iterator = nullptr;
        while (void* field = api.classFields(klass, &iterator)) {
            fputs("  field ", out);
            fputs((api.fieldFlags(field) & 0x10) ? "static " : "", out);
            WriteType(api, out, api.fieldType(field));
            const char* fieldName = api.fieldName(field);
            fprintf(out, " %s @0x%llX\n", fieldName ? fieldName : "?",
                    static_cast<unsigned long long>(api.fieldOffset(field)));
        }
        iterator = nullptr;
        while (void* method = api.classMethods(klass, &iterator)) {
            uint32_t implFlags = 0;
            const uint32_t flags = api.methodFlags(method, &implFlags);
            fputs("  method ", out);
            fputs((flags & 0x10) ? "static " : "", out);
            WriteType(api, out, api.methodReturnType(method));
            const char* methodName = api.methodName(method);
            fprintf(out, " %s(", methodName ? methodName : "?");
            const uint32_t count = api.methodParamCount(method);
            for (uint32_t i = 0; i < count && i < 32; ++i) {
                if (i) {
                    fputs(", ", out);
                }
                WriteType(api, out, api.methodParam(method, i));
            }
            fputc(')', out);
            // Offsets 0x0 and 0x58 are both recorded: this fingerprint's notes name +0x58 as the
            // native body pointer, while stock IL2CPP keeps the method pointer at +0.
            const auto* words = reinterpret_cast<const uintptr_t*>(method);
            WriteRva(out, "rva", words[0], image);
            WriteRva(out, "rva58", words[0x58 / sizeof(uintptr_t)], image);
            fputc('\n', out);
        }
    }

    inline bool WriteClassGuarded(const Api& api, FILE* out, void* klass, const Image& image) {
        __try {
            WriteClass(api, out, klass, image);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline bool PrepareGuarded(const Api& api, void*& domain) {
        __try {
            domain = api.domainGet();
            if (domain) {
                api.threadAttach(domain);
            }
            return domain != nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline bool StartsWith(const char* text, const char* prefix) {
        return text && _strnicmp(text, prefix, strlen(prefix)) == 0;
    }

    inline void Dump(const Api& api, void* domain, const Image& image, const std::wstring& directory) {
        const std::wstring classPath = directory + L"\\2026dump.txt";
        const std::wstring listPath = directory + L"\\2026dump-assemblies.txt";
        FILE* classes = _wfsopen(classPath.c_str(), L"wb", _SH_DENYNO);
        FILE* list = _wfsopen(listPath.c_str(), L"wb", _SH_DENYNO);
        if (!classes || !list) {
            PatchLogging::Log("[Dump] cannot create output files beside the DLL");
            if (classes) {
                fclose(classes);
            }
            if (list) {
                fclose(list);
            }
            return;
        }
        static char buffer[1 << 20];
        setvbuf(classes, buffer, _IOFBF, sizeof(buffer));
        fputs("// Rec Room 20260323.14 runtime class dump (names, types, offsets and RVAs only)\n", classes);
        size_t assemblyCount = 0;
        void** assemblies = api.domainGetAssemblies(domain, &assemblyCount);
        size_t dumped = 0, failed = 0;
        for (size_t a = 0; assemblies && a < assemblyCount; ++a) {
            void* img = api.assemblyGetImage(assemblies[a]);
            const char* imageName = img ? api.imageGetName(img) : nullptr;
            const size_t classCount = img ? api.imageGetClassCount(img) : 0;
            fprintf(list, "%s\t%llu\n", imageName ? imageName : "?",
                    static_cast<unsigned long long>(classCount));
            // Obfuscated game code lives in Assembly-CSharp. The framework assemblies are large and
            // have public names, so only the game's own assemblies are written in full.
            if (!StartsWith(imageName, "Assembly-CSharp")) {
                continue;
            }
            fprintf(classes, "\n// ===== %s (%llu classes) =====\n", imageName,
                    static_cast<unsigned long long>(classCount));
            for (size_t c = 0; c < classCount; ++c) {
                void* klass = api.imageGetClass(img, c);
                if (!klass) {
                    continue;
                }
                if (WriteClassGuarded(api, classes, klass, image)) {
                    ++dumped;
                } else {
                    ++failed;
                }
            }
        }
        fclose(classes);
        fclose(list);
        PatchLogging::Log("[Dump] complete: %llu assemblies, %llu classes written, %llu skipped after faults",
                          static_cast<unsigned long long>(assemblyCount),
                          static_cast<unsigned long long>(dumped), static_cast<unsigned long long>(failed));
    }

    // Rec Room's GameAssembly.dll exports no il2cpp_* names, so the functions have to be identified
    // another way. These two files record every export (name, ordinal, RVA) and every populated slot
    // of Unity's internal API table with the export, if any, that it points at.
    inline void WriteExports(HMODULE game, const std::wstring& directory) {
        const std::wstring path = directory + L"\\2026dump-exports.txt";
        FILE* out = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
        if (!out) {
            return;
        }
        const auto base = reinterpret_cast<const unsigned char*>(game);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& entry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!entry.VirtualAddress) {
            fputs("// no export directory\n", out);
            fclose(out);
            return;
        }
        const auto* dir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + entry.VirtualAddress);
        const auto* functions = reinterpret_cast<const DWORD*>(base + dir->AddressOfFunctions);
        const auto* names = reinterpret_cast<const DWORD*>(base + dir->AddressOfNames);
        const auto* ordinals = reinterpret_cast<const WORD*>(base + dir->AddressOfNameOrdinals);
        fprintf(out, "// functions=%lu names=%lu base=%lu\n", dir->NumberOfFunctions, dir->NumberOfNames,
                dir->Base);
        for (DWORD i = 0; i < dir->NumberOfNames; ++i) {
            const WORD index = ordinals[i];
            fprintf(out, "%lu\t0x%lX\t%s\n", dir->Base + index, functions[index],
                    reinterpret_cast<const char*>(base + names[i]));
        }
        fclose(out);
    }

    inline const char* ExportNameForRva(HMODULE game, DWORD rva) {
        const auto base = reinterpret_cast<const unsigned char*>(game);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& entry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!entry.VirtualAddress) {
            return nullptr;
        }
        const auto* dir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + entry.VirtualAddress);
        const auto* functions = reinterpret_cast<const DWORD*>(base + dir->AddressOfFunctions);
        const auto* names = reinterpret_cast<const DWORD*>(base + dir->AddressOfNames);
        const auto* ordinals = reinterpret_cast<const WORD*>(base + dir->AddressOfNameOrdinals);
        for (DWORD i = 0; i < dir->NumberOfNames; ++i) {
            if (functions[ordinals[i]] == rva) {
                return reinterpret_cast<const char*>(base + names[i]);
            }
        }
        return nullptr;
    }

    inline void WriteApiTable(HMODULE game, HMODULE unity, const std::wstring& directory) {
        const std::wstring path = directory + L"\\2026dump-api-table.txt";
        FILE* out = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
        if (!out) {
            return;
        }
        const auto gameBase = reinterpret_cast<uintptr_t>(game);
        const auto* table = reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(unity) +
                                                               RR::Methods::Unity::ApiTable);
        fputs("// slot\tGameAssembly RVA\texport name (if any)\n", out);
        unsigned zeros = 0;
        for (unsigned slot = 0; slot < 640 && zeros < 24; ++slot) {
            const uintptr_t value = table[slot];
            zeros = value ? 0 : zeros + 1;
            if (value >= gameBase && value < gameBase + RR::Methods::GameAssembly::ImageSize) {
                const DWORD rva = static_cast<DWORD>(value - gameBase);
                const char* name = ExportNameForRva(game, rva);
                fprintf(out, "%u\t0x%lX\t%s\n", slot, rva, name ? name : "");
            } else if (value) {
                fprintf(out, "%u\t(outside GameAssembly)\n", slot);
            }
        }
        fclose(out);
    }

    inline bool WriteApiFilesGuarded(HMODULE game, HMODULE unity, const std::wstring& directory) {
        __try {
            WriteExports(game, directory);
            WriteApiTable(game, unity, directory);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    struct Request {
        std::wstring directory;
    };

    // Runs on its own thread. IL2CPP is not safe to query until Unity has initialized it, so wait for
    // the same API-table entry the patch waits for, then allow the runtime time to finish starting.
    inline DWORD WINAPI Run(void* parameter) {
        const std::unique_ptr<Request> request(static_cast<Request*>(parameter));
        HMODULE game = nullptr;
        HMODULE unity = nullptr;
        const ULONGLONG deadline = GetTickCount64() + 180000;
        while (GetTickCount64() < deadline) {
            game = GetModuleHandleW(L"GameAssembly.dll");
            unity = GetModuleHandleW(L"UnityPlayer.dll");
            if (game && unity) {
                const auto* table = reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(unity) +
                                                                       RR::Methods::Unity::ApiTable);
                const auto base = reinterpret_cast<uintptr_t>(game);
                if (table[176] >= base && table[176] < base + RR::Methods::GameAssembly::ImageSize) {
                    break;
                }
            }
            Sleep(250);
            game = nullptr;
        }
        if (!game) {
            PatchLogging::Log("[Dump] IL2CPP never became ready; no dump written");
            return 1;
        }
        if (!WriteApiFilesGuarded(game, unity, request->directory)) {
            PatchLogging::Log("[Dump] could not read the export directory or API table");
        }
        Sleep(20000);
        Api api;
        if (!Resolve(game, api)) {
            PatchLogging::Log("[Dump] required IL2CPP exports are missing; no class dump written. "
                              "Send 2026dump-exports.txt and 2026dump-api-table.txt instead");
            return 2;
        }
        void* domain = nullptr;
        if (!PrepareGuarded(api, domain)) {
            PatchLogging::Log("[Dump] could not attach to the IL2CPP domain");
            return 3;
        }
        Image image;
        image.base = reinterpret_cast<uintptr_t>(game);
        image.end = image.base + RR::Methods::GameAssembly::ImageSize;
        PatchLogging::Log("[Dump] starting");
        Dump(api, domain, image, request->directory);
        return 0;
    }

    inline void Start(const std::wstring& directory) {
        auto* request = new Request{directory};
        const HANDLE thread = CreateThread(nullptr, 0, Run, request, 0, nullptr);
        if (!thread) {
            delete request;
            PatchLogging::Log("[Dump] could not start the dump thread");
            return;
        }
        CloseHandle(thread);
    }
}
