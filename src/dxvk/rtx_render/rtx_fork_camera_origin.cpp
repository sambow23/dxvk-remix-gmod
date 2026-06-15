#include "rtx_fork_camera_origin.h"

#include "rtx_fork_game_state.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <string>

namespace dxvk {
  namespace fork_camera_origin {

    bool tryParseWorldOriginComponent(const std::string& raw, float& outValue) {
      const char* const begin = raw.c_str();
      char* end = nullptr;
      errno = 0;
      const float parsed = std::strtof(begin, &end);
      if (end == begin || errno == ERANGE) {
        return false;
      }

      while (*end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) {
          return false;
        }
        ++end;
      }

      outValue = parsed;
      return true;
    }

    Vector3 readWorldOriginOffsetFromGameState() {
      std::string rawX;
      std::string rawY;
      std::string rawZ;
      auto& gameState = fork_game_state::GameStateStore::get();
      if (!gameState.tryGet(kWorldOriginGameValueX, rawX)
          || !gameState.tryGet(kWorldOriginGameValueY, rawY)
          || !gameState.tryGet(kWorldOriginGameValueZ, rawZ)) {
        return Vector3(0.0f);
      }

      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      if (!tryParseWorldOriginComponent(rawX, x)
          || !tryParseWorldOriginComponent(rawY, y)
          || !tryParseWorldOriginComponent(rawZ, z)) {
        return Vector3(0.0f);
      }

      return Vector3(x, y, z);
    }

  } // namespace fork_camera_origin
} // namespace dxvk
