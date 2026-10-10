#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <array>
#include <cmath>
#include <vector>
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "Dxgi.lib")

namespace DesktopCapture {
    inline void ResizeBgra(const BYTE* source, size_t pitch, int sourceWidth, int sourceHeight, int width,
                           int height, std::vector<unsigned char>& pixels) {
        pixels.resize(static_cast<size_t>(width) * height * 4);
        // Resize on the CPU rather than using GDI's screen StretchBlt or
        // GDI+ DrawImage. Fixed-point bilinear sampling preserves the caller's
        // dimensions and keeps this readback independent of game rendering.
        struct Column {
            int left, right, weight;
        };
        std::vector<Column> columns(width);
        for (int x = 0; x < width; ++x) {
            const auto position =
                std::clamp<int64_t>((static_cast<int64_t>(2 * x + 1) * sourceWidth * 128 / width) - 128, 0,
                                    static_cast<int64_t>(sourceWidth - 1) * 256);
            const int left = static_cast<int>(position / 256);
            columns[x] = {left * 4, (std::min)(left + 1, sourceWidth - 1) * 4,
                          static_cast<int>(position % 256)};
        }
        for (int y = 0; y < height; ++y) {
            const auto position =
                std::clamp<int64_t>((static_cast<int64_t>(2 * y + 1) * sourceHeight * 128 / height) - 128, 0,
                                    static_cast<int64_t>(sourceHeight - 1) * 256);
            const int top = static_cast<int>(position / 256);
            const int weight = static_cast<int>(position % 256);
            auto first = source + static_cast<size_t>(top) * pitch;
            auto second = source + static_cast<size_t>((std::min)(top + 1, sourceHeight - 1)) * pitch;
            auto destination = pixels.data() + static_cast<size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const auto& column = columns[x];
                for (int c = 0; c < 3; ++c) {
                    const int a = first[column.left + c] * (256 - column.weight) +
                                  first[column.right + c] * column.weight;
                    const int b = second[column.left + c] * (256 - column.weight) +
                                  second[column.right + c] * column.weight;
                    destination[x * 4 + c] =
                        static_cast<BYTE>((a * (256 - weight) + b * weight + 32768) >> 16);
                }
                destination[x * 4 + 3] = 255;
            }
        }
    }
    inline float SdrScale(const wchar_t* name) {
        UINT pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
            return 1.0f;
        }
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                               nullptr) != ERROR_SUCCESS) {
            return 1.0f;
        }
        for (UINT i = 0; i < pathCount; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                             paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                wcscmp(source.viewGdiDeviceName, name)) {
                continue;
            }
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white),
                            paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel) {
                return 1000.0f / white.SDRWhiteLevel;
            }
        }
        return 1.0f;
    }
    inline void ResizeScRgb(const BYTE* source, size_t pitch, int sourceWidth, int sourceHeight, int width,
                            int height, float whiteScale, std::vector<unsigned char>& pixels) {
        // HDR duplication supplies linear scRGB half-floats in RGBA order. Normalize
        // Windows' SDR white level, clamp HDR highlights, then encode sRGB for JPEG.
        // Lookup tables avoid per-pixel half conversion and pow on the capture task.
        static const auto linear = [] {
            std::array<float, 65536> values{};
            for (unsigned i = 0; i < values.size(); ++i) {
                const int exponent = (i >> 10) & 31;
                const int mantissa = i & 1023;
                if (exponent == 31) {
                    values[i] = (i & 0x8000 || mantissa) ? 0.0f : 65504.0f;
                } else {
                    values[i] = std::ldexp(exponent ? 1.0f + mantissa / 1024.0f : mantissa / 1024.0f,
                                           exponent ? exponent - 15 : -14) *
                                (i & 0x8000 ? -1.0f : 1.0f);
                }
            }
            return values;
        }();
        static const auto srgb = [] {
            std::array<BYTE, 4097> values{};
            for (unsigned i = 0; i < values.size(); ++i) {
                const float x = i / 4096.0f;
                const float gamma = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
                values[i] = static_cast<BYTE>(gamma * 255.0f + 0.5f);
            }
            return values;
        }();
        pixels.resize(static_cast<size_t>(width) * height * 4);
        struct Column {
            int left, right;
            float weight;
        };
        std::vector<Column> columns(width);
        for (int x = 0; x < width; ++x) {
            const float position = std::clamp((x + 0.5f) * sourceWidth / width - 0.5f, 0.0f,
                                              static_cast<float>(sourceWidth - 1));
            const int left = static_cast<int>(position);
            columns[x] = {left * 4, (std::min)(left + 1, sourceWidth - 1) * 4, position - left};
        }
        for (int y = 0; y < height; ++y) {
            const float position = std::clamp((y + 0.5f) * sourceHeight / height - 0.5f, 0.0f,
                                              static_cast<float>(sourceHeight - 1));
            const int top = static_cast<int>(position);
            const float weight = position - top;
            auto first = reinterpret_cast<const uint16_t*>(source + static_cast<size_t>(top) * pitch);
            auto second = reinterpret_cast<const uint16_t*>(
                source + static_cast<size_t>((std::min)(top + 1, sourceHeight - 1)) * pitch);
            auto destination = pixels.data() + static_cast<size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const auto& column = columns[x];
                for (int c = 0; c < 3; ++c) {
                    const float a = linear[first[column.left + c]] * (1 - column.weight) +
                                    linear[first[column.right + c]] * column.weight;
                    const float b = linear[second[column.left + c]] * (1 - column.weight) +
                                    linear[second[column.right + c]] * column.weight;
                    const float value = std::clamp((a * (1 - weight) + b * weight) * whiteScale, 0.0f, 1.0f);
                    destination[x * 4 + 2 - c] = srgb[static_cast<unsigned>(value * 4096 + 0.5f)];
                }
                destination[x * 4 + 3] = 255;
            }
        }
    }
    // Capture tasks are serialized by the client. This owns a separate D3D device;
    // it never calls the game's immediate context or changes its render state.
    class Duplication {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGIOutputDuplication> output;
        ComPtr<ID3D11Texture2D> staging;
        RECT monitor{};
        HRESULT status = S_OK;
        DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
        float whiteScale = 1.0f;
        double readbackMs = 0, resizeMs = 0;
        bool Check(HRESULT result) {
            status = result;
            return SUCCEEDED(result);
        }
        bool held = false;
        bool valid = false;
        ULONGLONG retryAt = 0;

        void Reset() {
            if (held && output) {
                output->ReleaseFrame();
            }
            held = false;
            valid = false;
            staging.Reset();
            output.Reset();
            context.Reset();
            device.Reset();
        }
        bool Initialize(const RECT& requested) {
            ComPtr<IDXGIFactory1> factory;
            if (!Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
                return false;
            }
            for (UINT a = 0;; ++a) {
                ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(a, &adapter) != S_OK) {
                    break;
                }
                for (UINT o = 0;; ++o) {
                    ComPtr<IDXGIOutput> candidate;
                    if (adapter->EnumOutputs(o, &candidate) != S_OK) {
                        break;
                    }
                    DXGI_OUTPUT_DESC description{};
                    if (!Check(candidate->GetDesc(&description)) || !description.AttachedToDesktop ||
                        !EqualRect(&requested, &description.DesktopCoordinates) ||
                        description.Rotation != DXGI_MODE_ROTATION_IDENTITY) {
                        continue;
                    }
                    ComPtr<IDXGIOutput1> selected;
                    if (!Check(candidate.As(&selected)) ||
                        !Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr,
                                                 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
                        return false;
                    }
                    if (!Check(selected->DuplicateOutput(device.Get(), &output))) {
                        return false;
                    }
                    whiteScale = SdrScale(description.DeviceName);
                    monitor = requested;
                    return true;
                }
            }
            status = DXGI_ERROR_NOT_FOUND;
            return false;
        }

      public:
        double ReadbackMilliseconds() const {
            return readbackMs;
        }
        double ResizeMilliseconds() const {
            return resizeMs;
        }
        HRESULT Status() const {
            return status;
        }
        ~Duplication() {
            Reset();
        }
        bool Capture(const RECT& requested, int width, int height, std::vector<unsigned char>& pixels) {
            readbackMs = resizeMs = 0;
            LARGE_INTEGER begin{}, mappedAt{}, resizedAt{}, frequency{};
            QueryPerformanceFrequency(&frequency);
            if (output && !EqualRect(&requested, &monitor)) {
                Reset();
                retryAt = 0;
            }
            if (!output) {
                if (GetTickCount64() < retryAt) {
                    return false;
                }
                if (!Initialize(requested)) {
                    Reset();
                    retryAt = GetTickCount64() + 5000;
                    return false;
                }
            }
            // Keep the acquired frame until the next request, as recommended by
            // DXGI. No polling or capture runs between the client's requests.
            if (held) {
                const HRESULT released = output->ReleaseFrame();
                status = released;
                held = false;
                if (FAILED(released)) {
                    Reset();
                    return false;
                }
            }
            QueryPerformanceCounter(&begin);
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> resource;
            const HRESULT result = output->AcquireNextFrame(0, &info, &resource);
            status = result;
            if (result == S_OK) {
                held = true;
                ComPtr<ID3D11Texture2D> texture;
                if (!Check(resource.As(&texture))) {
                    Reset();
                    return false;
                }
                D3D11_TEXTURE2D_DESC description{};
                texture->GetDesc(&description);
                if ((description.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
                     description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                    description.Width != static_cast<UINT>(requested.right - requested.left) ||
                    description.Height != static_cast<UINT>(requested.bottom - requested.top)) {
                    status = DXGI_ERROR_UNSUPPORTED;
                    Reset();
                    retryAt = GetTickCount64() + 5000;
                    return false;
                }
                if (staging && sourceFormat != description.Format) {
                    staging.Reset();
                    valid = false;
                }
                if (!staging) {
                    description.Usage = D3D11_USAGE_STAGING;
                    description.BindFlags = 0;
                    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    description.MiscFlags = 0;
                    if (!Check(device->CreateTexture2D(&description, nullptr, &staging))) {
                        Reset();
                        return false;
                    }
                }
                sourceFormat = description.Format;
                context->CopyResource(staging.Get(), texture.Get());
                valid = true;
            } else if (result != DXGI_ERROR_WAIT_TIMEOUT || !valid) {
                if (result != DXGI_ERROR_WAIT_TIMEOUT) {
                    Reset();
                    retryAt = GetTickCount64() + 1000;
                }
                return false;
            }
            // A timeout with a valid staging image means the desktop is unchanged.
            // Map may still wait for GPU readback; measure rather than assuming
            // this API makes capture free. GDI remains the compatibility fallback.
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (!Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                Reset();
                return false;
            }
            QueryPerformanceCounter(&mappedAt);
            struct Mapping {
                ID3D11DeviceContext* context;
                ID3D11Texture2D* texture;
                ~Mapping() {
                    context->Unmap(texture, 0);
                }
            } mapping{context.Get(), staging.Get()};
            if (sourceFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                ResizeScRgb(static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
                            requested.right - requested.left, requested.bottom - requested.top, width, height,
                            whiteScale, pixels);
            } else {
                ResizeBgra(static_cast<const BYTE*>(mapped.pData), mapped.RowPitch,
                           requested.right - requested.left, requested.bottom - requested.top, width, height,
                           pixels);
            }
            QueryPerformanceCounter(&resizedAt);
            readbackMs = (mappedAt.QuadPart - begin.QuadPart) * 1000.0 / frequency.QuadPart;
            resizeMs = (resizedAt.QuadPart - mappedAt.QuadPart) * 1000.0 / frequency.QuadPart;
            return true;
        }
    };
}
