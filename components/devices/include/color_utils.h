// AquaControl — HSV/RGB color utilities for perceptual fade interpolation.
//
// lerp_hsv() interpolates between two RGB colours through HSV space, giving
// perceptually smooth transitions that avoid the "grey mud" artefact produced
// by naive per-channel linear RGB interpolation.
#pragma once

#include <cstdint>

namespace aqua::devices {

/// Hue-Saturation-Value representation.  h in [0, 360), s and v in [0, 1].
struct Hsv { float h; float s; float v; };

/// 8-bit-per-channel RGB.
struct Rgb8 { uint8_t r; uint8_t g; uint8_t b; };

/// Standard RGB → HSV conversion.
Hsv  rgb_to_hsv(Rgb8 rgb);

/// Standard HSV → RGB conversion.
Rgb8 hsv_to_rgb(Hsv hsv);

/// Linearly interpolate from `from` to `to` through HSV space.
/// t == 0.0f returns `from`; t == 1.0f returns `to`.
/// Takes the shorter arc around the hue circle.
Rgb8 lerp_hsv(Rgb8 from, Rgb8 to, float t);

/// Interpolate directly between two Hsv values (no RGB round-trip).
/// Uses shortest-arc hue interpolation; output hue normalised to [0, 360).
Hsv lerp_hsv_native(Hsv from, Hsv to, float t);

}  // namespace aqua::devices
