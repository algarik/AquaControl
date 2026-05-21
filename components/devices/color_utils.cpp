#include "color_utils.h"

#include <algorithm>
#include <cmath>

namespace aqua::devices {

// ---------------------------------------------------------------------------
// RGB → HSV
// ---------------------------------------------------------------------------
Hsv rgb_to_hsv(Rgb8 rgb) {
    const float r = rgb.r / 255.0f;
    const float g = rgb.g / 255.0f;
    const float b = rgb.b / 255.0f;

    const float cmax = std::max({r, g, b});
    const float cmin = std::min({r, g, b});
    const float delta = cmax - cmin;

    Hsv out{};
    out.v = cmax;

    if (delta < 1e-6f) {
        // Achromatic — hue undefined, use 0
        return out;
    }

    out.s = delta / cmax;

    if (cmax == r) {
        out.h = 60.0f * std::fmod((g - b) / delta, 6.0f);
    } else if (cmax == g) {
        out.h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        out.h = 60.0f * ((r - g) / delta + 4.0f);
    }

    if (out.h < 0.0f) out.h += 360.0f;
    return out;
}

// ---------------------------------------------------------------------------
// HSV → RGB
// ---------------------------------------------------------------------------
Rgb8 hsv_to_rgb(Hsv hsv) {
    // Clamp inputs to valid ranges
    if (hsv.s < 1e-6f) {
        // Achromatic
        const uint8_t v = static_cast<uint8_t>(hsv.v * 255.0f + 0.5f);
        return {v, v, v};
    }

    const float h = hsv.h / 60.0f;
    const int   i = static_cast<int>(h);
    const float f = h - static_cast<float>(i);
    const float p = hsv.v * (1.0f - hsv.s);
    const float q = hsv.v * (1.0f - hsv.s * f);
    const float t = hsv.v * (1.0f - hsv.s * (1.0f - f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (i % 6) {
        case 0: r = hsv.v; g = t;     b = p;     break;
        case 1: r = q;     g = hsv.v; b = p;     break;
        case 2: r = p;     g = hsv.v; b = t;     break;
        case 3: r = p;     g = q;     b = hsv.v; break;
        case 4: r = t;     g = p;     b = hsv.v; break;
        case 5: r = hsv.v; g = p;     b = q;     break;
    }

    return {
        static_cast<uint8_t>(r * 255.0f + 0.5f),
        static_cast<uint8_t>(g * 255.0f + 0.5f),
        static_cast<uint8_t>(b * 255.0f + 0.5f),
    };
}

// ---------------------------------------------------------------------------
// lerp_hsv — interpolate through HSV, taking the shorter hue arc.
// ---------------------------------------------------------------------------
Rgb8 lerp_hsv(Rgb8 from, Rgb8 to, float t) {
    if (t <= 0.0f) return from;
    if (t >= 1.0f) return to;

    Hsv a = rgb_to_hsv(from);
    Hsv b = rgb_to_hsv(to);

    // Choose the shorter arc around the hue circle.
    float dh = b.h - a.h;
    if (dh >  180.0f) dh -= 360.0f;
    if (dh < -180.0f) dh += 360.0f;

    Hsv mid{};
    mid.h = a.h + dh * t;
    mid.s = a.s + (b.s - a.s) * t;
    mid.v = a.v + (b.v - a.v) * t;

    if (mid.h < 0.0f)   mid.h += 360.0f;
    if (mid.h >= 360.0f) mid.h -= 360.0f;

    return hsv_to_rgb(mid);
}

}  // namespace aqua::devices
