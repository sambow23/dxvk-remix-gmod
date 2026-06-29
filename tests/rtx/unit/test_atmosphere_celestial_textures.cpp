#include "../../../src/dxvk/rtx_render/rtx_fork_celestial_textures.h"
#include "../../../src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string readFile(const char* path) {
  std::ifstream file(std::string(BUILD_SOURCE_ROOT) + path, std::ios::binary);
  if (!file) {
    std::cerr << "failed to open " << path << '\n';
    std::exit(1);
  }

  return std::string(
      std::istreambuf_iterator<char>(file),
      std::istreambuf_iterator<char>());
}

void requireContains(const std::string& haystack, const char* needle, const char* message) {
  if (haystack.find(needle) == std::string::npos) {
    std::cerr << message << " missing: " << needle << '\n';
    std::exit(1);
  }
}

void requireNotContains(const std::string& haystack, const char* needle, const char* message) {
  if (haystack.find(needle) != std::string::npos) {
    std::cerr << message << " still present: " << needle << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace dxvk::fork_celestial_textures;

  require(kCelestialTextureFlagSun == 1u, "sun flag bit");
  require(kCelestialTextureFlagMoon0 == 2u, "moon flag bit");
  require(trimGameStateString("  C:/temp/sun.dds\r\n") == "C:/temp/sun.dds", "trim path");

  std::unordered_map<std::string, std::string> gameState {
      {kAtmosphereSunTextureGameStateKey, "  C:/mc/sun.dds "},
      {kAtmosphereMoon0TextureGameStateKey, "\tC:/mc/moon.dds\n"},
  };

  const auto paths = readCelestialTexturePaths(
      [&](std::string_view key, std::string& value) {
        const auto it = gameState.find(std::string(key));
        if (it == gameState.end()) {
          return false;
        }
        value = it->second;
        return true;
      });

  require(paths.sun == "C:/mc/sun.dds", "sun texture path parsed");
  require(paths.moon0 == "C:/mc/moon.dds", "moon texture path parsed");

  AtmosphereArgs args = {};
  require(args.celestialTextureFlags == 0u, "celestial flags default to zero");
  require(args.celestialTextureAngularRadius == 0.0f, "celestial texture radius default");
  require(args.celestialTextureBrightness == 0.0f, "celestial texture brightness default");
  require(sizeof(AtmosphereArgs) % 16u == 0u, "atmosphere args alignment");

  const std::string options = readFile("src/dxvk/rtx_render/rtx_options.h");
  requireContains(options,
                  "RTX_OPTION_ARGS(\"rtx.atmosphere\", float, celestialTextureSizeDeg, 10.0f,",
                  "celestial texture size option");
  requireContains(options,
                  "RTX_OPTION_ARGS(\"rtx.atmosphere\", float, celestialTextureBrightness, 1.0f,",
                  "celestial texture brightness option");
  requireContains(options,
                  "RTX_OPTION(\"rtx.atmosphere\", bool, celestialTextureNearestFiltering, true,",
                  "celestial texture nearest filtering option");

  const std::string atmosphereUi = readFile("src/dxvk/rtx_render/rtx_fork_atmosphere.cpp");
  requireContains(atmosphereUi, "Texture Size", "celestial texture size UI label");
  requireContains(atmosphereUi,
                  "&RtxOptions::celestialTextureSizeDegObject()",
                  "celestial texture size UI binding");
  requireContains(atmosphereUi, "Texture Brightness", "celestial texture brightness UI label");
  requireContains(atmosphereUi,
                  "&RtxOptions::celestialTextureBrightnessObject()",
                  "celestial texture brightness UI binding");
  requireContains(atmosphereUi, "Texture Nearest Filtering", "celestial texture filtering UI label");
  requireContains(atmosphereUi,
                  "&RtxOptions::celestialTextureNearestFilteringObject()",
                  "celestial texture filtering UI binding");
  requireContains(atmosphereUi,
                  "RtxOptions::celestialTextureNearestFiltering() ? VK_FILTER_NEAREST : VK_FILTER_LINEAR",
                  "celestial texture sampler filter selection");

  const std::string bindingIndices = readFile("src/dxvk/shaders/rtx/pass/common_binding_indices.h");
  requireContains(bindingIndices, "BINDING_ATMOSPHERE_SUN_TEXTURE", "sun binding index");
  requireContains(bindingIndices, "BINDING_ATMOSPHERE_MOON0_TEXTURE", "moon binding index");
  requireContains(bindingIndices, "BINDING_ATMOSPHERE_CELESTIAL_TEXTURE_SAMPLER", "celestial sampler binding index");

  const std::string commonBindings = readFile("src/dxvk/shaders/rtx/pass/common_bindings.slangh");
  requireContains(commonBindings, "AtmosphereSunTexture", "common sun texture binding");
  requireContains(commonBindings, "AtmosphereMoon0Texture", "common moon texture binding");
  requireContains(commonBindings, "AtmosphereCelestialTextureSampler", "common celestial sampler binding");

  const std::string atmosphereBindings = readFile("src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_bindings.slangh");
  requireContains(atmosphereBindings, "AtmosphereSunTexture", "atmosphere sun texture binding");
  requireContains(atmosphereBindings, "AtmosphereMoon0Texture", "atmosphere moon texture binding");
  requireContains(atmosphereBindings, "AtmosphereCelestialTextureSampler", "atmosphere celestial sampler binding");

  const std::string skyShader = readFile("src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh");
  requireContains(skyShader, "CELESTIAL_TEXTURE_FLAG_SUN", "sky shader sun flag");
  requireContains(skyShader, "CELESTIAL_TEXTURE_FLAG_MOON0", "sky shader moon flag");
  requireContains(skyShader, "computeCelestialSpriteUv", "sky shader sprite UV helper");
  requireContains(skyShader, "celestialTextureCoverage", "sky shader luminance-aware coverage");
  requireContains(skyShader, "evalSunTextureSprite", "sky shader sun texture sprite");
  requireContains(skyShader, "sampleMoon0TextureSprite", "sky shader moon texture sprite");
  requireContains(skyShader, "args.celestialTextureAngularRadius", "sky shader visual sprite radius");
  requireContains(skyShader, "args.celestialTextureBrightness", "sky shader texture brightness");
  requireNotContains(skyShader,
                     "computeCelestialDiskUv(viewDir, sunDir, args.sunAngularRadius",
                     "sun texture path should not use physical sun radius");

  return 0;
}
