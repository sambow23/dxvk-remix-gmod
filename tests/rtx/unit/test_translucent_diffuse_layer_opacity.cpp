#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

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

}  // namespace

int main() {
  const std::string remixApi = readFile("public/include/remix/remix_c.h");
  requireContains(remixApi, "#define REMIXAPI_VERSION_PATCH 5", "versioned translucent material extension");
  requireContains(remixApi, "float               diffuseLayerOpacity;", "public diffuse layer opacity");

  const std::string materialData = readFile("src/dxvk/rtx_render/rtx_material_data.h");
  requireContains(materialData, "X(DiffuseLayerOpacity", "translucent material opacity data");

  const std::string remixApiRuntime = readFile("src/dxvk/rtx_render/rtx_remix_api.cpp");
  requireContains(remixApiRuntime,
                  "REMIXAPI_VERSION_MAKE(0, 6, 5)",
                  "legacy API diffuse opacity fallback");

  const std::string cpuMaterial = readFile("src/dxvk/rtx_render/rtx_materials.h");
  requireContains(cpuMaterial,
                  "m_diffuseLayerOpacity",
                  "CPU translucent material opacity packing");

  const std::string gpuMaterial = readFile("src/dxvk/shaders/rtx/concept/surface_material/surface_material.h");
  requireContains(gpuMaterial,
                  "float16_t diffuseLayerOpacity;",
                  "GPU translucent material opacity field");

  const std::string interaction = readFile(
      "src/dxvk/shaders/rtx/concept/surface_material/translucent_surface_material_interaction.slangh");
  requireContains(interaction,
                  "transmittanceOrDiffuseSample.a * translucentSurfaceMaterial.diffuseLayerOpacity",
                  "diffuse texture opacity scaling");

  return 0;
}
