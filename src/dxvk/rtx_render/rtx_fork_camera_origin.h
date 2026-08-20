#pragma once

#include "../util/util_vector.h"

#include <string>

namespace dxvk {
  namespace fork_camera_origin {

    inline constexpr const char* kWorldOriginGameValueX = "__mcrtx.world_origin.x";
    inline constexpr const char* kWorldOriginGameValueY = "__mcrtx.world_origin.y";
    inline constexpr const char* kWorldOriginGameValueZ = "__mcrtx.world_origin.z";
    inline constexpr const char* kStableCameraGameValueX = "__mcrtx.camera_position.x";
    inline constexpr const char* kStableCameraGameValueY = "__mcrtx.camera_position.y";
    inline constexpr const char* kStableCameraGameValueZ = "__mcrtx.camera_position.z";

    bool tryParseWorldOriginComponent(const std::string& raw, float& outValue);
    Vector3 readWorldOriginOffsetFromGameState();
    bool tryReadStableCameraPositionFromGameState(Vector3& outPosition);
    Vector3 calculatePreviousCameraHistoryOffset(
      bool hasPreviousOrigin,
      const Vector3& previousOrigin,
      const Vector3& currentOrigin);

  } // namespace fork_camera_origin
} // namespace dxvk
