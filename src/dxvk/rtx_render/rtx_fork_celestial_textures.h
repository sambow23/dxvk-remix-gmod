#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace dxvk::fork_celestial_textures {

inline constexpr const char* kAtmosphereSunTextureGameStateKey = "__atmosphere.sun.texture";
inline constexpr const char* kAtmosphereMoon0TextureGameStateKey = "__atmosphere.moon0.texture";
inline constexpr uint32_t kCelestialTextureFlagSun = 1u << 0u;
inline constexpr uint32_t kCelestialTextureFlagMoon0 = 1u << 1u;

struct CelestialTexturePaths {
  std::string sun;
  std::string moon0;
};

using GameStateLookup = std::function<bool(std::string_view key, std::string& value)>;

std::string trimGameStateString(std::string_view raw);

CelestialTexturePaths readCelestialTexturePaths(const GameStateLookup& lookup);

}  // namespace dxvk::fork_celestial_textures
