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

#include <array>
#include <cstring>
#include <iostream>

#include "../../../src/dxvk/rtx_render/rtx_materials.h"
#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include "../../../src/lssusd/usd_include_end.h"

namespace dxvk {
  Logger Logger::s_instance("test_translucent_material.log");
}

namespace {

  bool testMaterialData() {
    dxvk::TranslucentMaterialData materialData;
    if (materialData.getEnableTransmissionMask()) {
      std::cerr << "Transmission mask must default to false\n";
      return false;
    }

    const XXH64_hash_t defaultHash = materialData.getHash();
    materialData.setEnableTransmissionMask(true);
    if (!materialData.getEnableTransmissionMask() || materialData.getHash() == defaultHash) {
      std::cerr << "Enabling the transmission mask must update the material and its hash\n";
      return false;
    }

    if (dxvk::TranslucentMaterialData::getEnableTransmissionMaskToken() !=
        pxr::TfToken("inputs:enable_transmission_mask")) {
      std::cerr << "Unexpected USD token for transmission mask\n";
      return false;
    }

    const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("translucent_material.usda");
    const pxr::UsdPrim shader = stage->DefinePrim(pxr::SdfPath("/Shader"), pxr::TfToken("Shader"));
    shader.CreateAttribute(
      dxvk::TranslucentMaterialData::getEnableTransmissionMaskToken(),
      pxr::SdfValueTypeNames->Bool).Set(true);

    const auto getTexture = [](const pxr::UsdPrim&, const pxr::TfToken&) {
      return dxvk::TextureRef {};
    };
    const dxvk::TranslucentMaterialData deserialized =
      dxvk::TranslucentMaterialData::deserialize(getTexture, shader);
    if (!deserialized.getEnableTransmissionMask()) {
      std::cerr << "Authored transmission mask did not survive USD deserialization\n";
      return false;
    }

    return true;
  }

  bool testGpuFlag() {
    const auto makeMaterial = [](bool enableTransmissionMask) {
      return dxvk::RtTranslucentSurfaceMaterial {
        dxvk::kSurfaceMaterialInvalidTextureIndex,
        dxvk::kSurfaceMaterialInvalidTextureIndex,
        dxvk::kSurfaceMaterialInvalidTextureIndex,
        1.52f,
        1.0f,
        dxvk::Vector3(0.97f),
        false,
        0.0f,
        dxvk::Vector3(0.0f),
        false,
        0.001f,
        true,
        enableTransmissionMask,
        dxvk::kSurfaceMaterialInvalidTextureIndex
      };
    };

    const auto readFlags = [](const dxvk::RtTranslucentSurfaceMaterial& material) {
      std::array<unsigned char, dxvk::kSurfaceMaterialGPUSize> data {};
      std::size_t offset = 0;
      material.writeGPUData(data.data(), offset, 0);

      uint16_t flags = 0;
      std::memcpy(&flags, data.data(), sizeof(flags));
      return flags;
    };

    const uint16_t disabledFlags = readFlags(makeMaterial(false));
    const uint16_t enabledFlags = readFlags(makeMaterial(true));
    if (disabledFlags & TRANSLUCENT_SURFACE_MATERIAL_FLAG_ENABLE_TRANSMISSION_MASK) {
      std::cerr << "Disabled transmission mask unexpectedly set the GPU flag\n";
      return false;
    }
    if (!(enabledFlags & TRANSLUCENT_SURFACE_MATERIAL_FLAG_ENABLE_TRANSMISSION_MASK)) {
      std::cerr << "Enabled transmission mask did not set the GPU flag\n";
      return false;
    }

    return true;
  }

}

int main() {
  return testMaterialData() && testGpuFlag() ? 0 : -1;
}
