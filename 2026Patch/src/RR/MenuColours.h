#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Menu colour scheme: four gradient stops and an accent, as #rrggbb. The launcher writes the same five
// values it saves to the account. This file holds only the parsing and the colour maths, so it has no
// game dependencies.
//
// Two modes. Light (the default) recolours only the game's orange: gradients and solid colours take the
// scheme, everything else keeps the game's colours. Dark additionally turns light, slightly tinted
// surfaces near-black and dark text white, and uses the scheme colours exactly as given.
namespace RR::MenuColours {
    constexpr int Count = 5;

    struct Rgb {
        float r = 0, g = 0, b = 0;
    };

    struct Scheme {
        bool enabled = false;
        bool dark = false;
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

    // The built-in dark scheme, used when MenuMode=dark and no MenuColours are given.
    inline Scheme DarkScheme() {
        Scheme scheme;
        Parse("#2d0f5c,#3b1578,#4a1c94,#5a24b0,#4a1c94", scheme);
        scheme.dark = true;
        return scheme;
    }

    // MenuMode: empty or "light" for the default, "dark" for dark mode. Dark with no colours of its own
    // takes the built-in dark purples. Returns false for any other text.
    inline bool ApplyMode(const std::string& text, Scheme& scheme) {
        std::string mode;
        for (const char c : text) {
            if (c != ' ' && c != '\t') {
                mode += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
        }
        if (mode.empty() || mode == "light") {
            return true;
        }
        if (mode != "dark") {
            return false;
        }
        if (!scheme.enabled) {
            scheme = DarkScheme();
        }
        scheme.dark = true;
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

    // The game's own menu colour is orange (observed gradient keys run from hue 0.03 to 0.09, saturation
    // 0.5 to 1; pure red sits at hue 0 and yellow at 0.17, so both are excluded).
    inline bool IsMenuOrange(const Hsv& hsv) {
        return hsv.s >= 0.4f && hsv.v >= 0.5f && (hsv.h >= 0.015f && hsv.h <= 0.13f);
    }

    // Light mode matches the original brightness, so a dark entry stays dark and a bright one bright. Dark
    // mode takes the scheme colour exactly.
    inline void Tint(const Scheme& scheme, const Rgb& target, float brightness, float* rgba) {
        if (scheme.dark) {
            rgba[0] = target.r;
            rgba[1] = target.g;
            rgba[2] = target.b;
            return;
        }
        const float peak = (std::max)({target.r, target.g, target.b, 0.01f});
        const float scale = (std::min)((std::max)(brightness / peak, 0.3f), 1.5f);
        rgba[0] = (std::min)(target.r * scale, 1.0f);
        rgba[1] = (std::min)(target.g * scale, 1.0f);
        rgba[2] = (std::min)(target.b * scale, 1.0f);
    }

    // A solid menu colour takes the accent. Text on a dark surface takes a light tint of it instead, since a
    // dark purple word on a dark purple card could not be read.
    inline bool RemapSolid(const Scheme& scheme, float* rgba, bool text = false) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (!IsMenuOrange(hsv)) {
            return false;
        }
        if (scheme.dark && text) {
            const Rgb& a = scheme.accent;
            rgba[0] = a.r + (1.0f - a.r) * 0.65f;
            rgba[1] = a.g + (1.0f - a.g) * 0.65f;
            rgba[2] = a.b + (1.0f - a.b) * 0.65f;
            return true;
        }
        Tint(scheme, scheme.accent, hsv.v, rgba);
        return true;
    }

    // A gradient key takes the scheme gradient at its own position (0 = start, 1 = end).
    inline bool RemapGradientKey(const Scheme& scheme, float* rgba, float time) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (!IsMenuOrange(hsv)) {
            return false;
        }
        Tint(scheme, AlongGradient(scheme, time), hsv.v, rgba);
        return true;
    }

    inline bool IsPureWhite(const float* rgba) {
        return rgba[0] > 0.995f && rgba[1] > 0.995f && rgba[2] > 0.995f;
    }

    // Dark mode, surfaces: the game's cards, panels and buttons are cream or pale pink (light, a little
    // tinted). Pure white is left alone because it is the default tint of every sprite, icon and avatar
    // picture, and white text.
    inline bool RemapDarkSurface(float* rgba) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (hsv.v < 0.8f || hsv.s > 0.2f || IsPureWhite(rgba)) {
            return false;
        }
        rgba[0] = 0.07f;
        rgba[1] = 0.06f;
        rgba[2] = 0.10f;
        return true;
    }

    // Dark mode, text: dark, nearly grey text becomes white. Coloured and light text is left as it is.
    inline bool RemapDarkText(float* rgba) {
        const Hsv hsv = ToHsv({rgba[0], rgba[1], rgba[2]});
        if (hsv.v > 0.45f || hsv.s > 0.35f) {
            return false;
        }
        rgba[0] = rgba[1] = rgba[2] = 1.0f;
        return true;
    }

    // One call for every solid-colour setter. `text` is true for the text setters.
    inline bool RemapUi(const Scheme& scheme, float* rgba, bool text) {
        if (!scheme.enabled) {
            return false;
        }
        if (RemapSolid(scheme, rgba, text)) {
            return true;
        }
        if (!scheme.dark) {
            return false;
        }
        return text ? RemapDarkText(rgba) : RemapDarkSurface(rgba);
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
