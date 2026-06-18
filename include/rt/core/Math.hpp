#pragma once

#include <array>

namespace rt {

using Mat3x4 = std::array<float, 12>;

constexpr Mat3x4 identityTransform3x4() noexcept
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
}

} // namespace rt
