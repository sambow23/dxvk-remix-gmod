#pragma once

#include "../util/util_vector.h"

#include <string>

namespace dxvk {
  namespace fork_camera_origin {

    inline constexpr const char* kWorldOriginGameValueX = "__mcrtx.world_origin.x";
    inline constexpr const char* kWorldOriginGameValueY = "__mcrtx.world_origin.y";
    inline constexpr const char* kWorldOriginGameValueZ = "__mcrtx.world_origin.z";

    bool tryParseWorldOriginComponent(const std::string& raw, float& outValue);
    Vector3 readWorldOriginOffsetFromGameState();

  } // namespace fork_camera_origin
} // namespace dxvk
