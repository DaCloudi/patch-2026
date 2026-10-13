#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Menu colour scheme: four gradient stops and an accent, as #rrggbb. The launcher writes the same five
// values it saves to the account. This file holds only the parsing and the colour maths, so it has no
// game dependencies.
namespace RR::MenuColours {
    constexpr int Count = 5;

    struct Rgb {
        float r = 0, g = 0, b = 0;
    };

    struct Scheme {
        bool enabled = false;
        Rgb stops[4];
        Rgb accent;
    };

    inline bool ParseHex(const std::string& text, Rgb& out) {
        if (text.size() != 7 || text[0] != '#') {
            return false;
        }
        unsigned value = 0;
        for (size_t i = 1; i < 7; ++i) {
            const char c = text[i];
            unsigned digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
            value = value * 16 + digit;
        }
        out.r = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
        out.g = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
        out.b = static_cast<float>(value & 0xFF) / 255.0f;
        return true;
    }

    // Empty text is "off". Otherwise exactly five comma-separated #rrggbb values, or false.
    inline bool Parse(const std::string& text, Scheme& scheme) {
        scheme = Scheme{};
        std::string trimmed;
        for (const char c : text) {
            if (c != ' ' && c != '\t') {
                trimmed += c;
            }
        }
        if (trimmed.empty()) {
            return true;
        }
        Rgb parsed[Count];
        size_t start = 0;
        for (int i = 0; i < Count; ++i) {
            const size_t end = i == Count - 1 ? trimmed.size() : trimmed.find(',', start);
            if (end == std::string::npos || !ParseHex(trimmed.substr(start, end - start), parsed[i])) {
                return false;
            }
            if (i == Count - 1 && trimmed.find(',', start) != std::string::npos) {
                return false;
            }
            start = end + 1;
        }
        for (int i = 0; i < 4; ++i) {
            scheme.stops[i] = parsed[i];
        }
        scheme.accent = parsed[4];
        scheme.enabled = true;
        return true;
    }

    struct Hsv {
        float h = 0, s = 0, v = 0;
    };

    inline Hsv ToHsv(const Rgb& c) {
        const float high = (std::max)({c.r, c.g, c.b});
        const float low = (std::min)({c.r, c.g, c.b});
        const float delta = high - low;
        Hsv out;
        out.v = high;
        out.s = high > 0 ? delta / high : 0;
        if (delta > 0) {
            if (high == c.r) {
                out.h = std::fmod((c.g - c.b) / delta, 6.0f);
            } else if (high == c.g) {
                out.h = (c.b - c.r) / delta + 2.0f;
            } else {
                out.h = (c.r - c.g) / delta + 4.0f;
            }
            out.h /= 6.0f;
            if (out.h < 0) {
                out.h += 1.0f;
            }
        }
        return out;
    }

    // The gradient colour at position t in [0, 1] across the four stops.
    inline Rgb AlongGradient(const Scheme& scheme, float t) {
        t = (std::min)((std::max)(t, 0.0f), 1.0f) * 3.0f;
        const int index = (std::min)(static_cast<int>(t), 2);
        const float f = t - static_cast<float>(index);
        const Rgb& a = scheme.stops[index];
        const Rgb& b = scheme.stops[index + 1];
        return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
    }

    // Recolours one theme colour. Greys and near-black/white are left alone, so text and backgrounds stay
    // readable; a coloured entry takes the scheme colour at its own hue's position around the colour
    // wheel, scaled to its original brightness. Returns true if the colour changed.
    inline bool Remap(const Scheme& scheme, float* rgba) {
        const Rgb in{rgba[0], rgba[1], rgba[2]};
        const Hsv hsv = ToHsv(in);
        if (hsv.s < 0.25f || hsv.v < 0.2f) {
            return false;
        }
        const Rgb target = AlongGradient(scheme, hsv.h);
        const float peak = (std::max)({target.r, target.g, target.b, 0.01f});
        const float scale = (std::min)((std::max)(hsv.v / peak, 0.3f), 1.5f);
        rgba[0] = (std::min)(target.r * scale, 1.0f);
        rgba[1] = (std::min)(target.g * scale, 1.0f);
        rgba[2] = (std::min)(target.b * scale, 1.0f);
        return true;
    }

    // The game's own menu colour is orange (observed gradient keys run from hue 0.03 to 0.09, saturation
    // 0.5 to 1; pure red sits at hue 0 and yellow at 0.17, so both are excluded). Only that family is
    // recoloured, so avatar swatches, effects and other UI keep their colours.
    inline bool IsMenuOrange(const Hsv& hsv) {
        return hsv.s >= 0.4f && hsv.v >= 0.5f && (hsv.h >= 0.015f && hsv.h <= 0.13f);
    }

    inline void Tint(const Rgb& target, float brightness, float* rgba) {
        const float peak = (std::max)({target.r, target.g, target.b, 0.01f});
        const float scale = (std::min)((std::max)(brightness / peak, 0.3f), 1.5f);
        rgba[0] = (std::min)(target.r * scale, 1.0f);
        rgba[1] = (std::min)(target.g * scale, 1.0f);
        rgba[2] = (std::min)(target.b * scale, 1.0f);
    }

    // A solid menu colour takes the accent.
    inline bool RemapSolid(const Scheme& scheme, float* rgba) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (!IsMenuOrange(hsv)) {
            return false;
        }
        Tint(scheme.accent, hsv.v, rgba);
        return true;
    }

    // A gradient key takes the scheme gradient at its own position (0 = start, 1 = end).
    inline bool RemapGradientKey(const Scheme& scheme, float* rgba, float time) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (!IsMenuOrange(hsv)) {
            return false;
        }
        Tint(AlongGradient(scheme, time), hsv.v, rgba);
        return true;
    }

    // A packed Color32 vertex colour (r in the low byte). Returns true if the orange was remapped; alpha is
    // kept. Same orange-only rule as the solid setters, so a colour a setter already changed is left alone.
    inline bool RemapVertex(const Scheme& scheme, uint32_t& packed) {
        float rgba[4] = {static_cast<float>(packed & 0xFF) / 255.0f,
                         static_cast<float>((packed >> 8) & 0xFF) / 255.0f,
                         static_cast<float>((packed >> 16) & 0xFF) / 255.0f, 1.0f};
        if (!scheme.enabled || !RemapSolid(scheme, rgba)) {
            return false;
        }
        const auto byte = [](float v) {
            return static_cast<uint32_t>((std::min)((std::max)(v, 0.0f), 1.0f) * 255.0f + 0.5f);
        };
        packed = (packed & 0xFF000000u) | (byte(rgba[2]) << 16) | (byte(rgba[1]) << 8) | byte(rgba[0]);
        return true;
    }

    // A value that really is a float colour component. Guards against reading a different struct.
    inline bool SaneColour(const float* rgba) {
        for (int i = 0; i < 4; ++i) {
            if (!std::isfinite(rgba[i]) || rgba[i] < 0.0f || rgba[i] > 1.0001f) {
                return false;
            }
        }
        return true;
    }
}
