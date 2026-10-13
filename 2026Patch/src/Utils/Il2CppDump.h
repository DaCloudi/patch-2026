#pragma once

#include <windows.h>
#include <algorithm>
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
        bool classes = false;
        bool metadata = false;
    };

    // Waits until Unity has populated its IL2CPP API table (the entry the patch itself waits for),
    // which happens as the runtime starts. Returns false if that never happens.
    inline bool WaitForIl2Cpp(HMODULE& game, HMODULE& unity) {
        const ULONGLONG deadline = GetTickCount64() + 180000;
        while (GetTickCount64() < deadline) {
            game = GetModuleHandleW(L"GameAssembly.dll");
            unity = GetModuleHandleW(L"UnityPlayer.dll");
            if (game && unity) {
                const auto* table = reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(unity) +
                                                                       RR::Methods::Unity::ApiTable);
                const auto base = reinterpret_cast<uintptr_t>(game);
                if (table[176] >= base && table[176] < base + RR::Methods::GameAssembly::ImageSize) {
                    return true;
                }
            }
            Sleep(250);
        }
        return false;
    }

    // The shipped global-metadata.dat is a placeholder. The game maps the real data read-only, with
    // the first few hundred bytes scrambled, and keeps a decoded copy of the header elsewhere in
    // memory (magic AF 1B B1 FA, then a version, then (offset, size) pairs). Find the mapping, find
    // a header copy whose offsets fit inside it, and splice the two into a normal metadata file.
    struct Blob {
        const unsigned char* base = nullptr;
        size_t size = 0;
    };

    constexpr int HeaderPairs = 31; // 8 + 31 * 8 = 0x100, the size of the metadata header
    constexpr int HeaderInts = HeaderPairs * 2;

    // The tables are laid out in a different order from the header's fields, so the (offset, size)
    // pairs do not ascend. The mapping's own header holds the right numbers in a scrambled order,
    // so the decoded copy is recognised by holding the same set of numbers.
    struct Fingerprint {
        uint64_t sum = 0;
        uint64_t xored = 0;
        uint64_t squares = 0;
    };

    inline Fingerprint FingerprintOf(const unsigned char* ints) {
        Fingerprint fp;
        for (int i = 0; i < HeaderInts; ++i) {
            uint32_t value = 0;
            memcpy(&value, ints + i * 4, 4);
            fp.sum += value;
            fp.xored ^= value;
            fp.squares += static_cast<uint64_t>(value) * value;
        }
        return fp;
    }

    // The end of the data when every pair lies inside a blob of this size, otherwise 0.
    inline size_t PairsEnd(const unsigned char* pairs, size_t blobSize) {
        size_t end = 0;
        for (int pair = 0; pair < HeaderPairs; ++pair) {
            int32_t offset = 0, size = 0;
            memcpy(&offset, pairs + pair * 8, 4);
            memcpy(&size, pairs + pair * 8 + 4, 4);
            if (offset < 0 || size < 0 ||
                static_cast<size_t>(offset) + static_cast<size_t>(size) > blobSize) {
                return 0;
            }
            end = (std::max)(end, static_cast<size_t>(offset) + static_cast<size_t>(size));
        }
        return end;
    }

    // No C++ objects with destructors, so it can contain the SEH block. Slides a 62-number window over
    // the region and compares running totals before checking anything else.
    inline bool ScanHeaderGuarded(const unsigned char* begin, size_t length, size_t blobSize,
                                  const Fingerprint& want, const unsigned char** found, size_t* end) {
        __try {
            const size_t words = length / 4;
            if (words < static_cast<size_t>(HeaderInts)) {
                return false;
            }
            const auto* data = reinterpret_cast<const uint32_t*>(begin);
            Fingerprint have;
            for (size_t i = 0; i < words; ++i) {
                const uint32_t added = data[i];
                have.sum += added;
                have.xored ^= added;
                have.squares += static_cast<uint64_t>(added) * added;
                if (i >= static_cast<size_t>(HeaderInts)) {
                    const uint32_t removed = data[i - HeaderInts];
                    have.sum -= removed;
                    have.xored ^= removed;
                    have.squares -= static_cast<uint64_t>(removed) * removed;
                }
                if (i + 1 >= static_cast<size_t>(HeaderInts) && have.sum == want.sum &&
                    have.xored == want.xored && have.squares == want.squares) {
                    const unsigned char* pairs = begin + (i + 1 - HeaderInts) * 4;
                    if (const size_t e = PairsEnd(pairs, blobSize)) {
                        *found = pairs;
                        *end = e;
                        return true;
                    }
                }
            }
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline bool WriteGuarded(FILE* out, const unsigned char* data, size_t size) {
        __try {
            return fwrite(data, 1, size, out) == size;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Fallback when no header is found: describe the large readable regions and save any that hold the
    // metadata string table (it contains "Assembly-CSharp.dll"), so the layout can be studied offline.
    inline bool RegionHasMarker(const unsigned char* begin, size_t length, const char* marker,
                                size_t markerLength) {
        __try {
            for (size_t i = 0; i + markerLength < length; ++i) {
                if (begin[i] == static_cast<unsigned char>(marker[0]) &&
                    memcmp(begin + i, marker, markerLength) == 0) {
                    return true;
                }
            }
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline bool CountMagicGuarded(const unsigned char* begin, size_t length, int* total, int* versions) {
        __try {
            for (size_t i = 0; i + 8 < length; ++i) {
                if (begin[i] == 0xAF && begin[i + 1] == 0x1B && begin[i + 2] == 0xB1 &&
                    begin[i + 3] == 0xFA) {
                    ++*total;
                    int32_t version = 0;
                    memcpy(&version, begin + i + 4, 4);
                    if (*versions < 6) {
                        PatchLogging::Log("[Metadata]   magic at +0x%llX version=%d",
                                          static_cast<unsigned long long>(i), version);
                        ++*versions;
                    }
                }
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    inline void DescribeRegions(const std::wstring& directory) {
        MEMORY_BASIC_INFORMATION info{};
        const unsigned char* cursor = nullptr;
        int logged = 0, saved = 0;
        while (VirtualQuery(cursor, &info, sizeof(info)) == sizeof(info)) {
            const auto* begin = static_cast<const unsigned char*>(info.BaseAddress);
            const bool readable =
                info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            if (readable && info.RegionSize >= (16u << 20) && logged < 40) {
                ++logged;
                int total = 0, versions = 0;
                CountMagicGuarded(begin, info.RegionSize, &total, &versions);
                const bool marker = RegionHasMarker(begin, info.RegionSize, "Assembly-CSharp.dll", 19);
                unsigned char head[16]{};
                memcpy(head, begin, 16);
                PatchLogging::Log("[Metadata] region %p size=%llu type=0x%lX protect=0x%lX magic=%d "
                                  "marker=%d head=%02X%02X%02X%02X%02X%02X%02X%02X",
                                  static_cast<const void*>(begin),
                                  static_cast<unsigned long long>(info.RegionSize), info.Type, info.Protect,
                                  total, marker ? 1 : 0, head[0], head[1], head[2], head[3], head[4], head[5],
                                  head[6], head[7]);
                if (marker && saved < 2 && info.RegionSize <= (160u << 20)) {
                    const std::wstring path =
                        directory + L"\\2026metadata-region" + std::to_wstring(saved) + L".bin";
                    if (FILE* out = _wfsopen(path.c_str(), L"wb", _SH_DENYNO)) {
                        const bool ok = WriteGuarded(out, begin, info.RegionSize);
                        fclose(out);
                        PatchLogging::Log("[Metadata]   saved region to %d (%s)", saved,
                                          ok ? "ok" : "failed");
                    }
                    ++saved;
                }
            }
            cursor = begin + info.RegionSize;
            if (cursor <= begin) {
                break;
            }
        }
        PatchLogging::Log("[Metadata] region survey done (%d large regions listed, %d saved)", logged, saved);
    }

    inline bool ReadableRegion(const MEMORY_BASIC_INFORMATION& info) {
        return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
               (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

    inline bool MemoryReadable(const void* address, size_t length) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || !ReadableRegion(info)) {
            return false;
        }
        const auto* end = static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize;
        return static_cast<const unsigned char*>(address) + length <= end;
    }

    // Mapped file views that hold the metadata string table (usually one, but other mappings of the
    // game's own files can contain the same name).
    inline int FindMappedBlobs(Blob* blobs, int capacity) {
        int count = 0;
        MEMORY_BASIC_INFORMATION info{};
        const unsigned char* cursor = nullptr;
        while (count < capacity && VirtualQuery(cursor, &info, sizeof(info)) == sizeof(info)) {
            const auto* begin = static_cast<const unsigned char*>(info.BaseAddress);
            if (ReadableRegion(info) && info.Type == MEM_MAPPED && info.RegionSize >= (20u << 20) &&
                info.RegionSize <= (200u << 20) &&
                RegionHasMarker(begin, info.RegionSize, "Assembly-CSharp.dll", 19)) {
                blobs[count].base = begin;
                blobs[count].size = info.RegionSize;
                ++count;
            }
            cursor = begin + info.RegionSize;
            if (cursor <= begin) {
                break;
            }
        }
        return count;
    }

    inline void DumpMetadata(const std::wstring& directory) {
        Blob blobs[8];
        const int blobCount = FindMappedBlobs(blobs, 8);
        if (!blobCount) {
            PatchLogging::Log("[Metadata] no mapped metadata found; surveying large regions");
            DescribeRegions(directory);
            return;
        }
        for (int i = 0; i < blobCount; ++i) {
            PatchLogging::Log("[Metadata] mapped candidate %d at %p, %llu bytes", i,
                              static_cast<const void*>(blobs[i].base),
                              static_cast<unsigned long long>(blobs[i].size));
        }
        // Look for the decoded table of (offset, size) pairs that matches one of the mappings.
        Blob blob;
        Fingerprint wants[8];
        for (int i = 0; i < blobCount; ++i) {
            wants[i] = FingerprintOf(blobs[i].base + 8);
        }
        const unsigned char* pairs = nullptr;
        size_t end = 0;
        MEMORY_BASIC_INFORMATION info{};
        const unsigned char* cursor = nullptr;
        while (!pairs && VirtualQuery(cursor, &info, sizeof(info)) == sizeof(info)) {
            const auto* begin = static_cast<const unsigned char*>(info.BaseAddress);
            if (ReadableRegion(info)) {
                bool isBlob = false;
                for (int i = 0; i < blobCount; ++i) {
                    isBlob = isBlob || blobs[i].base == begin;
                }
                for (int i = 0; i < blobCount && !isBlob && !pairs; ++i) {
                    if (ScanHeaderGuarded(begin, info.RegionSize, blobs[i].size, wants[i], &pairs, &end)) {
                        blob = blobs[i];
                    }
                }
            }
            cursor = begin + info.RegionSize;
            if (cursor <= begin) {
                break;
            }
        }
        if (!pairs) {
            PatchLogging::Log("[Metadata] no decoded header found (no block holds the same 62 numbers as the "
                              "mapping's header); saving the mapped data instead");
            DescribeRegions(directory);
            return;
        }
        // A magic and version, if the copy kept them just before the pairs, are used as they are.
        int32_t version = 31;
        bool hasMagic = false;
        if (MemoryReadable(pairs - 8, 8) && pairs[-8] == 0xAF && pairs[-7] == 0x1B && pairs[-6] == 0xB1 &&
            pairs[-5] == 0xFA) {
            hasMagic = true;
            memcpy(&version, pairs - 4, 4);
        }
        PatchLogging::Log("[Metadata] header table at %p (magic %s, version %d), data ends at %llu of %llu",
                          static_cast<const void*>(pairs), hasMagic ? "present" : "absent", version,
                          static_cast<unsigned long long>(end), static_cast<unsigned long long>(blob.size));
        for (int i = 0; i < HeaderPairs; ++i) {
            int32_t offset = 0, size = 0;
            memcpy(&offset, pairs + i * 8, 4);
            memcpy(&size, pairs + i * 8 + 4, 4);
            PatchLogging::Log("[Metadata]   pair %d: offset=%d size=%d", i, offset, size);
        }
        const std::wstring path = directory + L"\\2026metadata.dat";
        FILE* out = _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
        if (!out) {
            PatchLogging::Log("[Metadata] cannot create 2026metadata.dat beside the DLL");
            return;
        }
        // Rebuild the standard header (magic, version, pairs), then the mapping's own data after it.
        unsigned char header[0x100]{};
        header[0] = 0xAF;
        header[1] = 0x1B;
        header[2] = 0xB1;
        header[3] = 0xFA;
        memcpy(header + 4, &version, 4);
        memcpy(header + 8, pairs, HeaderPairs * 8);
        const size_t length = (std::min)(end, blob.size);
        const bool ok = WriteGuarded(out, header, sizeof(header)) &&
                        WriteGuarded(out, blob.base + sizeof(header), length - sizeof(header));
        fclose(out);
        PatchLogging::Log("[Metadata] %s 2026metadata.dat (version %d, %llu bytes)",
                          ok ? "wrote" : "FAILED writing", version, static_cast<unsigned long long>(length));
    }

    // Runs on its own thread. IL2CPP is not safe to query until Unity has initialized it, so wait for
    // the API table, then allow the runtime time to finish starting.
    inline DWORD WINAPI Run(void* parameter) {
        const std::unique_ptr<Request> request(static_cast<Request*>(parameter));
        HMODULE game = nullptr;
        HMODULE unity = nullptr;
        if (!WaitForIl2Cpp(game, unity)) {
            PatchLogging::Log("[Dump] IL2CPP never became ready; no dump written");
            return 1;
        }
        if (request->metadata) {
            Sleep(15000);
            DumpMetadata(request->directory);
        }
        if (!request->classes) {
            return 0;
        }
        if (!WriteApiFilesGuarded(game, unity, request->directory)) {
            PatchLogging::Log("[Dump] could not read the export directory or API table");
        }
        Sleep(5000);
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

    inline void Start(const std::wstring& directory, bool classes, bool metadata) {
        auto* request = new Request{directory, classes, metadata};
        const HANDLE thread = CreateThread(nullptr, 0, Run, request, 0, nullptr);
        if (!thread) {
            delete request;
            PatchLogging::Log("[Dump] could not start the dump thread");
            return;
        }
        CloseHandle(thread);
    }
}
