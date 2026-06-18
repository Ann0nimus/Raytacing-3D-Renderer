#pragma once

#include <array>
#include <cmath>

namespace rt {

using Mat3x4 = std::array<float, 12>;
using Vec3 = std::array<float, 3>;

constexpr Mat3x4 identityTransform3x4() noexcept
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
}

inline Vec3 add(Vec3 a, Vec3 b) noexcept
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

inline Vec3 sub(Vec3 a, Vec3 b) noexcept
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline Vec3 mul(Vec3 value, float scale) noexcept
{
    return {value[0] * scale, value[1] * scale, value[2] * scale};
}

inline float dot(Vec3 a, Vec3 b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
}

inline Vec3 normalize(Vec3 value) noexcept
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.000001F) {
        return {0.0F, 0.0F, 0.0F};
    }
    return mul(value, 1.0F / length);
}

} // namespace rt
