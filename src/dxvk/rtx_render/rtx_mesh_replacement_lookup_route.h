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
#pragma once

#include <cstdint>

namespace dxvk {

  enum class MeshReplacementLookupRoute : uint8_t {
    None,
    Direct,
    ExplicitAlias,
    AutomaticGeometry,
    LegacyAsset0,
    LegacyAsset1,
  };

  inline constexpr const char* getMeshReplacementLookupRouteName(
    MeshReplacementLookupRoute route) {
    switch (route) {
    case MeshReplacementLookupRoute::Direct:
      return "direct";
    case MeshReplacementLookupRoute::ExplicitAlias:
      return "explicit-alias";
    case MeshReplacementLookupRoute::AutomaticGeometry:
      return "automatic-geometry";
    case MeshReplacementLookupRoute::LegacyAsset0:
      return "legacy-asset-0";
    case MeshReplacementLookupRoute::LegacyAsset1:
      return "legacy-asset-1";
    case MeshReplacementLookupRoute::None:
    default:
      return "none";
    }
  }

}
