/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "../../../src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using Color = std::array<float, 3>;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void requireNear(const Color& actual, const Color& expected, const char* message) {
  for (size_t i = 0; i < actual.size(); ++i) {
    if (std::fabs(actual[i] - expected[i]) > 0.0001f) {
      std::cerr << message << " channel " << i << ": "
                << actual[i] << " != " << expected[i] << '\n';
      std::exit(1);
    }
  }
}

Color add(const Color& a, const Color& b) {
  return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

Color subtract(const Color& a, const Color& b) {
  return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Color multiply(const Color& color, float scalar) {
  return { color[0] * scalar, color[1] * scalar, color[2] * scalar };
}

Color fog(const Color& radiance, const Color& fogColor, float fogOpacity) {
  return add(multiply(radiance, 1.0f - fogOpacity),
             multiply(fogColor, fogOpacity));
}

Color compositeCloud(const Color& background,
                     const Color& cloudPremultiplied,
                     float cloudOpacity,
                     float cloudBrightness) {
  return add(multiply(background, 1.0f - cloudOpacity),
             multiply(cloudPremultiplied, cloudBrightness));
}

Color restoreCloudAboveFog(const Color& foggedCloudComposite,
                           const Color& fogColor,
                           float fogOpacity,
                           const Color& cloudPremultiplied,
                           float cloudOpacity,
                           float cloudBrightness,
                           float horizonVisibility = 1.0f) {
  const Color visibleCloud = multiply(cloudPremultiplied, cloudBrightness);
  const Color correction = subtract(visibleCloud, multiply(fogColor, cloudOpacity));
  return add(foggedCloudComposite,
             multiply(correction, fogOpacity * horizonVisibility));
}

void checkFogCase(const Color& background,
                  const Color& fogColor,
                  const Color& cloudPremultiplied,
                  float cloudOpacity,
                  float fogOpacity,
                  float cloudBrightness,
                  float horizonVisibility,
                  const char* message) {
  const Color cloudComposite = compositeCloud(
      background, cloudPremultiplied, cloudOpacity, cloudBrightness);
  const Color fullyFogged = fog(cloudComposite, fogColor, fogOpacity);
  const Color actual = restoreCloudAboveFog(
      fog(cloudComposite, fogColor, fogOpacity),
      fogColor, fogOpacity, cloudPremultiplied, cloudOpacity, cloudBrightness,
      horizonVisibility);
  const Color fullyAboveFog = compositeCloud(
      fog(background, fogColor, fogOpacity),
      cloudPremultiplied, cloudOpacity, cloudBrightness);
  const Color expected = add(multiply(fullyFogged, 1.0f - horizonVisibility),
                             multiply(fullyAboveFog, horizonVisibility));
  requireNear(actual, expected, message);
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
    std::cerr << message << " unexpectedly contains: " << needle << '\n';
    std::exit(1);
  }
}

float cloudTranslationHistoryWeight(float travelKm) {
  return std::exp2(-travelKm / 0.000025f);
}

void testFogCorrectionMath() {
  const Color background { 0.1f, 0.2f, 0.4f };
  const Color fogColor { 0.7f, 0.6f, 0.5f };
  const Color cloudPremultiplied { 0.36f, 0.30f, 0.24f };

  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.0f, 1.0f, 1.0f, "zero fog");
  checkFogCase(background, fogColor, Color { 0.0f, 0.0f, 0.0f },
               0.0f, 0.75f, 1.0f, 1.0f, "zero cloud opacity");
  checkFogCase(background, fogColor, cloudPremultiplied,
               1.0f, 0.75f, 1.0f, 1.0f, "opaque cloud");
  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.75f, 1.0f, 1.0f, "partial cloud opacity");
  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.75f, 0.0f, 1.0f, "zero brightness");
  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.75f, 2.0f, 1.0f, "increased brightness");
  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.75f, 1.0f, 0.0f, "fully horizon-fogged cloud");
  checkFogCase(background, fogColor, cloudPremultiplied,
               0.6f, 0.75f, 1.0f, 0.35f, "partial horizon blend");

  const Color opaqueCloud = compositeCloud(
      background, cloudPremultiplied, 1.0f, 1.0f);
  const Color correctedOpaque = restoreCloudAboveFog(
      fog(opaqueCloud, fogColor, 0.75f),
      fogColor, 0.75f, cloudPremultiplied, 1.0f, 1.0f);
  requireNear(correctedOpaque, cloudPremultiplied,
              "opaque cloud radiance must be independent of depth fog");
}

void testTranslationHistoryWeight() {
  require(std::fabs(cloudTranslationHistoryWeight(0.0f) - 1.0f) < 0.0001f,
          "stationary camera must retain full cloud history");
  require(std::fabs(cloudTranslationHistoryWeight(0.000025f) - 0.5f) < 0.0001f,
          "2.5 cm travel must halve cloud history");
  require(std::fabs(cloudTranslationHistoryWeight(0.0001f) - 0.0625f) < 0.0001f,
          "10 cm travel must nearly retire cloud history");
}

void testSourceIntegration() {
  require(sizeof(AtmosphereArgs) % 16u == 0u,
          "AtmosphereArgs must remain 16-byte aligned");

  const std::string atmosphereHeader = readFile("src/dxvk/rtx_render/rtx_atmosphere.h");
  requireContains(atmosphereHeader,
                  "RTX_OPTION_ARGS(\"rtx.atmosphere\", float, cloudBrightness, 1.0f,",
                  "cloud brightness option");
  requireContains(atmosphereHeader, "args.minValue = 0.0f",
                  "cloud brightness minimum");
  requireContains(atmosphereHeader, "args.maxValue = 8.0f",
                  "cloud brightness maximum");
  const std::string optionDocumentation = readFile("RtxOptions.md");
  requireContains(optionDocumentation,
                  "|rtx.atmosphere.cloudBrightness|float|1|0|8|",
                  "cloud brightness option documentation");

  const std::string atmosphereArgs = readFile(
      "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_args.h");
  requireContains(atmosphereArgs, "float cloudBrightness;",
                  "cloud brightness shader argument");
  requireContains(atmosphereArgs, "float cloudCameraTravelKm;",
                  "stable camera travel shader argument");
  requireContains(atmosphereArgs, "float padCloudBrightness2;",
                  "cloud brightness argument alignment padding");

  const std::string atmosphere = readFile("src/dxvk/rtx_render/rtx_atmosphere.cpp");
  requireContains(atmosphere, "args.cloudBrightness             = 0.0f;",
                  "cloud brightness LUT-key normalization");
  requireContains(atmosphere, "args.cloudCameraTravelKm         = 0.0f;",
                  "cloud camera travel LUT-key normalization");
  requireContains(atmosphere,
                  "args.cloudBrightness         = std::min(std::max(cloudBrightness(), 0.0f), 8.0f);",
                  "cloud brightness argument clamp");

  const std::string atmosphereUi = readFile("src/dxvk/rtx_render/rtx_fork_atmosphere.cpp");
  requireContains(atmosphereUi,
                  "DragFloat(\"Brightness\", &RtxAtmosphere::cloudBrightnessObject()",
                  "cloud brightness UI binding");

  const std::string skyShader = readFile(
      "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_sky.slangh");
  const size_t historyWrite = skyShader.find(
      "AtmosphereCloudHistoryCurr[pixelCoord] = smoothedCloud;");
  const size_t brightnessApply = skyShader.find(
      "smoothedCloud.rgb * args.cloudBrightness");
  require(historyWrite != std::string::npos && brightnessApply != std::string::npos
              && historyWrite < brightnessApply,
          "cloud brightness must be applied after unscaled history is written");
  requireContains(skyShader, "cloudLayer.rgb * args.cloudBrightness",
                  "secondary cloud brightness");
  requireContains(skyShader,
                  "-args.cloudCameraTravelKm / 0.000025f",
                  "logical-camera translation history rejection");

  const std::string compositeShader = readFile(
      "src/dxvk/shaders/rtx/pass/composite/composite.comp.slang");
  const size_t volumetricExit = compositeShader.find("if (cb.volumeArgs.enable)");
  const size_t cloudCorrection = compositeShader.find(
      "AtmosphereCloudHistoryCurr[pixelCoordinate]");
  require(volumetricExit != std::string::npos && cloudCorrection != std::string::npos
              && volumetricExit < cloudCorrection,
          "cloud correction must remain outside volumetric fog");
  requireContains(compositeShader,
                  "fogColor.a * cloudHorizonVisibility",
                  "cloud depth-fog correction");
  requireContains(compositeShader,
                  "cloudDistanceKm - verticalDistanceKm",
                  "cloud horizon slant-distance blend");
  requireContains(compositeShader,
                  "cb.atmosphereArgs.cloudRenderRTEnable != 0u",
                  "cloud correction primary-render gate");
  requireContains(compositeShader,
                  "(cb.atmosphereArgs.debugSkyBisectFlags & 2u) == 0u",
                  "cloud correction stale-history diagnostic gate");

  const std::string atmosphereCommon = readFile(
      "src/dxvk/shaders/rtx/pass/atmosphere/atmosphere_common.slangh");
  requireContains(atmosphereCommon,
                  "- vec3(args.cameraWorldPosYUpKm.x, 0.0f, args.cameraWorldPosYUpKm.z)",
                  "camera-local cloud shell geometry");
  requireContains(atmosphereCommon,
                  "vec3(0.0f, args.cameraWorldPosYUpKm.y, 0.0f)",
                  "cloud shell camera height");

  const std::string geometryResolver = readFile(
      "src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh");
  requireContains(geometryResolver,
                  "const vec2 cloudMotion = calcMotionVectorForRayMiss",
                  "rotation-only cloud history reprojection");
  requireNotContains(geometryResolver,
                     "calcMotionVectorForCloudLayer",
                     "finite-depth cloud reprojection");

  const std::string cameraOriginHeader = readFile(
      "src/dxvk/rtx_render/rtx_fork_camera_origin.h");
  requireContains(cameraOriginHeader,
                  "__mcrtx.camera_position.x",
                  "stable camera game-value contract");

  const std::string composite = readFile("src/dxvk/rtx_render/rtx_composite.cpp");
  requireContains(composite,
                  "RW_TEXTURE2D(BINDING_ATMOSPHERE_CLOUD_HISTORY_CURR)",
                  "composite current cloud-history binding declaration");
  const std::string forkAtmosphere = readFile("src/dxvk/rtx_render/rtx_fork_atmosphere.cpp");
  requireContains(forkAtmosphere,
                  "bindResourceView(BINDING_ATMOSPHERE_CLOUD_HISTORY_CURR, cloudCurr.view",
                  "composite current cloud-history resource binding");
  requireContains(forkAtmosphere,
                  "tryReadStableCameraPositionFromGameState",
                  "stable camera cloud anchor selection");
}

} // anonymous namespace

int main() {
  std::cout << "Begin Numos cloud depth fog tests" << std::endl;
  testFogCorrectionMath();
  testTranslationHistoryWeight();
  testSourceIntegration();
  std::cout << "All Numos cloud depth fog tests passed" << std::endl;
  return 0;
}
