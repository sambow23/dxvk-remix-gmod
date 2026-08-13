#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace dxvk {
  /**
    * \brief Finds the single effective bone used by an indexed-blend stream
    *
    * D3D9 stores one fewer explicit weight than bone indices; the final
    * weight is the remainder to 1. Unused indices are frequently padded with
    * zero, so index equality alone cannot identify a rigid mesh.
    *
    * \returns: The effective bone index, or -1 if the stream is empty,
    *           invalid, or uses more than one positively weighted bone.
    */
  inline int32_t getRigidBoneIndex(
      const uint8_t* indexPtr,
      uint32_t indexStride,
      const uint8_t* weightPtr,
      uint32_t weightStride,
      uint32_t vertexCount,
      uint32_t numBonesPerVertex) {
    if (indexPtr == nullptr || weightPtr == nullptr || vertexCount == 0
     || numBonesPerVertex == 0 || numBonesPerVertex > 4
     || indexStride < numBonesPerVertex
     || weightStride < sizeof(float) * (numBonesPerVertex - 1)) {
      return -1;
    }

    int32_t rigidBoneIndex = -1;
    for (uint32_t vertex = 0; vertex < vertexCount; vertex++) {
      float remainingWeight = 1.f;
      for (uint32_t component = 0; component < numBonesPerVertex; component++) {
        float weight = remainingWeight;
        if (component + 1 < numBonesPerVertex) {
          std::memcpy(&weight, weightPtr + component * sizeof(float), sizeof(weight));
          if (!std::isfinite(weight)) {
            return -1;
          }
          remainingWeight -= weight;
        }

        // Match the skinning shader: zero and negative influences are ignored.
        if (weight > 0.f) {
          const int32_t boneIndex = indexPtr[component];
          if (rigidBoneIndex < 0) {
            rigidBoneIndex = boneIndex;
          } else if (rigidBoneIndex != boneIndex) {
            return -1;
          }
        }
      }

      indexPtr += indexStride;
      weightPtr += weightStride;
    }

    return rigidBoneIndex;
  }
}
