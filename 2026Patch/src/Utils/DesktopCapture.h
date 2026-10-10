#pragma once
#include <algorithm>
#include <cstdint>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Ole32.lib")

// The client ships System.Drawing.Common's unsupported-platform stub. Keep its
// existing selected-monitor, output-width and JPEG-quality contract, using the
// Windows encoder directly. Nothing is captured until the game requests a frame.
namespace DesktopCapture {
    struct Bounds {
        int x, y, width, height;
    };
    inline bool Encode(const Bounds& bounds, int width, int quality, std::vector<unsigned char>& jpeg) {
        // Reject degenerate or excessive allocations before creating GDI objects.
        // Compute height in 64 bits to preserve monitor aspect ratio without
        // overflow. Monitor origins may be negative on a multi-display desktop.
        jpeg.clear();
        if (bounds.width <= 0 || bounds.height <= 0 || bounds.width > 32768 || bounds.height > 32768 ||
            width <= 0 || width > 4096) {
            return false;
        }
        const int height = static_cast<int>(static_cast<int64_t>(width) * bounds.height / bounds.width);
        if (height <= 0 || height > 4096) {
            return false;
        }
        HDC desktop = GetDC(nullptr);
        if (!desktop) {
            return false;
        }
        HDC destination = CreateCompatibleDC(desktop);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        // A negative DIB height is top-down, matching the encoder's positive
        // stride; using a bottom-up bitmap here would vertically invert the frame.
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        bool success = false;
        if (destination && bitmap && pixels) {
            HGDIOBJ previous = SelectObject(destination, bitmap);
            SetStretchBltMode(destination, HALFTONE);
            SetBrushOrgEx(destination, 0, 0, nullptr);
            if (StretchBlt(destination, 0, 0, width, height, desktop, bounds.x, bounds.y, bounds.width,
                           bounds.height, SRCCOPY | CAPTUREBLT)) {
                GdiFlush();
                Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppRGB,
                                      static_cast<BYTE*>(pixels));
                UINT count = 0, size = 0;
                if (Gdiplus::GetImageEncodersSize(&count, &size) == Gdiplus::Ok && size) {
                    std::vector<unsigned char> storage(size);
                    auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
                    if (Gdiplus::GetImageEncoders(count, size, encoders) == Gdiplus::Ok) {
                        for (UINT i = 0; i < count; ++i) {
                            if (wcscmp(encoders[i].MimeType, L"image/jpeg")) {
                                continue;
                            }
                            IStream* stream = nullptr;
                            // TRUE makes the stream own its backing HGLOBAL.
                            // Bound the encoded result before copying into the
                            // managed byte array returned by the capture hook.
                            if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
                                ULONG level = static_cast<ULONG>(std::clamp(quality, 0, 100));
                                Gdiplus::EncoderParameters parameters{};
                                parameters.Count = 1;
                                parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
                                parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                                parameters.Parameter[0].NumberOfValues = 1;
                                parameters.Parameter[0].Value = &level;
                                STATSTG stat{};
                                if (image.Save(stream, &encoders[i].Clsid, &parameters) == Gdiplus::Ok &&
                                    SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
                                    stat.cbSize.QuadPart > 0 && stat.cbSize.QuadPart <= 4 * 1024 * 1024) {
                                    LARGE_INTEGER start{};
                                    ULONG read = 0;
                                    jpeg.resize(static_cast<size_t>(stat.cbSize.QuadPart));
                                    success = SUCCEEDED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) &&
                                              SUCCEEDED(stream->Read(
                                                  jpeg.data(), static_cast<ULONG>(jpeg.size()), &read)) &&
                                              read == jpeg.size();
                                }
                                stream->Release();
                            }
                            break;
                        }
                    }
                }
            }
            // Restore the old selection before deleting the bitmap. Every frame
            // releases its DC, bitmap and stream on both success and failure;
            // leaked GDI handles eventually break an otherwise healthy stream.
            SelectObject(destination, previous);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (destination) {
            DeleteDC(destination);
        }
        ReleaseDC(nullptr, desktop);
        if (!success) {
            jpeg.clear();
        }
        return success;
    }
}
