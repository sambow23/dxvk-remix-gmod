#include "rtx_fork_celestial_textures.h"

namespace dxvk::fork_celestial_textures {
namespace {

bool readPath(const GameStateLookup& lookup, std::string_view key, std::string& outPath) {
  std::string raw;
  if (!lookup(key, raw)) {
    outPath.clear();
    return false;
  }

  outPath = trimGameStateString(raw);
  return !outPath.empty();
}

}  // namespace

std::string trimGameStateString(std::string_view raw) {
  constexpr std::string_view kWhitespace = " \t\r\n";
  const size_t begin = raw.find_first_not_of(kWhitespace);
  if (begin == std::string_view::npos) {
    return {};
  }

  const size_t end = raw.find_last_not_of(kWhitespace);
  return std::string(raw.substr(begin, end - begin + 1u));
}

CelestialTexturePaths readCelestialTexturePaths(const GameStateLookup& lookup) {
  CelestialTexturePaths paths;
  readPath(lookup, kAtmosphereSunTextureGameStateKey, paths.sun);
  readPath(lookup, kAtmosphereMoon0TextureGameStateKey, paths.moon0);
  return paths;
}

}  // namespace dxvk::fork_celestial_textures
