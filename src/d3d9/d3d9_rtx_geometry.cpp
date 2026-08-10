#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

#include "d3d9_device.h"
#include "d3d9_rtx.h"
#include "d3d9_rtx_utils.h"
#include "d3d9_state.h"
#include "../dxvk/dxvk_buffer.h"
#include "../dxvk/dxvk_objects.h"
#include "../dxvk/rtx_render/rtx_hashing.h"
#include "../dxvk/rtx_render/rtx_resources.h"
#include "../util/util_fastops.h"

namespace dxvk {
  // Geometry indices should never be signed.  Using this to handle the non-indexed case for templates.
  typedef int NoIndices;

  namespace VertexRegions {
    enum Type : uint32_t {
      Position = 0,
      Texcoord,
      Count
    };
  }

  // NOTE: Intentionally leaving the legacy hashes out of here, because they are special (REMIX-656)
  const std::map<HashComponents, VertexRegions::Type> componentToRegionMap = {
    { HashComponents::VertexPosition,   VertexRegions::Position },
    { HashComponents::VertexTexcoord,   VertexRegions::Texcoord },
  };

  namespace {
    using ExactPosition = std::array<uint32_t, 3>;
    using QuantizedPosition = std::array<int64_t, 3>;
    using ExactTriangle = std::array<uint32_t, 9>;
    using QuantizedTriangle = std::array<int64_t, 9>;

    ExactPosition readPositionBits(const HashQuery& positions, uint32_t vertexIndex) {
      ExactPosition result {};
      std::memcpy(result.data(), positions.pBase + vertexIndex * positions.stride, sizeof(result));
      return result;
    }

    std::array<float, 3> positionBitsToFloats(const ExactPosition& bits) {
      std::array<float, 3> result {};
      std::memcpy(result.data(), bits.data(), sizeof(result));
      return result;
    }

    bool quantizePosition(const std::array<float, 3>& position, QuantizedPosition& result) {
      for (uint32_t component = 0; component < result.size(); component++) {
        if (!std::isfinite(position[component])) {
          return false;
        }

        const double scaled = static_cast<double>(position[component]) / GeometryHashDebugData::QuantizationStep;
        if (scaled < static_cast<double>(std::numeric_limits<int64_t>::min())
            || scaled > static_cast<double>(std::numeric_limits<int64_t>::max())) {
          return false;
        }
        result[component] = static_cast<int64_t>(std::llround(scaled));
      }
      return true;
    }

    template<typename T>
    std::shared_ptr<const GeometryHashDebugData> buildGeometryHashDebugData(
        size_t indexCount,
        const void* pIndexData,
        const HashQuery& positions,
        const std::vector<T>& uniqueIndices,
        VkPrimitiveTopology topology) {
      constexpr bool hasIndices = !std::is_same_v<T, NoIndices>;

      auto result = std::make_shared<GeometryHashDebugData>();
      result->streamVertexCount = positions.stride != 0
        ? static_cast<uint32_t>(positions.size / positions.stride)
        : 0;
      result->positionFormatSupported = positions.pBase != nullptr
        && positions.elementSize >= sizeof(float) * 3
        && positions.stride >= sizeof(float) * 3;

      if (!result->positionFormatSupported) {
        return result;
      }

      result->positionSampleCount = std::min(
        result->streamVertexCount,
        GeometryHashDebugData::MaxPositionSamples);
      for (uint32_t i = 0; i < result->positionSampleCount; i++) {
        result->positionSampleBits[i] = readPositionBits(positions, i);
      }

      if constexpr (hasIndices) {
        if (pIndexData == nullptr) {
          return result;
        }
        result->indexSampleCount = std::min(
          static_cast<uint32_t>(indexCount),
          GeometryHashDebugData::MaxIndexSamples);
        const T* pIndices = static_cast<const T*>(pIndexData);
        for (uint32_t i = 0; i < result->indexSampleCount; i++) {
          result->indexSamples[i] = static_cast<uint32_t>(pIndices[i]);
        }
        for (size_t i = 0; i < indexCount; i++) {
          if (static_cast<uint32_t>(pIndices[i]) >= result->streamVertexCount) {
            result->invalidIndexCount++;
          }
        }
      }

      std::vector<uint32_t> referencedIndices;
      if constexpr (hasIndices) {
        referencedIndices.reserve(uniqueIndices.size());
        for (T index : uniqueIndices) {
          referencedIndices.push_back(static_cast<uint32_t>(index));
        }
      } else {
        referencedIndices.resize(result->streamVertexCount);
        for (uint32_t i = 0; i < result->streamVertexCount; i++) {
          referencedIndices[i] = i;
        }
      }
      result->referencedVertexCount = static_cast<uint32_t>(referencedIndices.size());

      std::vector<ExactPosition> exactPositions;
      std::vector<QuantizedPosition> quantizedPositions;
      exactPositions.reserve(referencedIndices.size());
      quantizedPositions.reserve(referencedIndices.size());

      std::array<float, 3> boundsMin {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
      };
      std::array<float, 3> boundsMax {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
      };

      for (uint32_t index : referencedIndices) {
        if (index >= result->streamVertexCount) {
          continue;
        }

        const ExactPosition exact = readPositionBits(positions, index);
        const std::array<float, 3> position = positionBitsToFloats(exact);
        exactPositions.push_back(exact);

        QuantizedPosition quantized {};
        if (quantizePosition(position, quantized)) {
          quantizedPositions.push_back(quantized);
          for (uint32_t component = 0; component < position.size(); component++) {
            boundsMin[component] = std::min(boundsMin[component], position[component]);
            boundsMax[component] = std::max(boundsMax[component], position[component]);
          }
        } else {
          result->nonFinitePositionCount++;
        }
      }

      result->positionCanonicalizationValid = result->invalidIndexCount == 0
        && !exactPositions.empty();
      result->quantizedCanonicalizationValid = result->positionCanonicalizationValid
        && result->nonFinitePositionCount == 0
        && quantizedPositions.size() == exactPositions.size();

      if (result->positionCanonicalizationValid) {
        std::sort(exactPositions.begin(), exactPositions.end());
        result->canonicalPositionHashExact = XXH3_64bits(
          exactPositions.data(),
          exactPositions.size() * sizeof(exactPositions[0]));
      }
      if (result->quantizedCanonicalizationValid) {
        std::sort(quantizedPositions.begin(), quantizedPositions.end());
        result->canonicalPositionHashQuantized = XXH3_64bits(
          quantizedPositions.data(),
          quantizedPositions.size() * sizeof(quantizedPositions[0]));
        result->boundsMin = boundsMin;
        result->boundsMax = boundsMax;
      }

      if (topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
        return result;
      }

      const uint32_t elementCount = hasIndices
        ? static_cast<uint32_t>(indexCount)
        : result->streamVertexCount;
      result->triangleCount = elementCount / 3;

      std::vector<ExactTriangle> exactTriangles;
      std::vector<QuantizedTriangle> quantizedTriangles;
      exactTriangles.reserve(result->triangleCount);
      quantizedTriangles.reserve(result->triangleCount);

      auto getIndex = [pIndexData](uint32_t element) {
        if constexpr (!std::is_same_v<T, NoIndices>) {
          return static_cast<uint32_t>(static_cast<const T*>(pIndexData)[element]);
        } else {
          return element;
        }
      };

      bool allTrianglePositionsQuantized = true;
      for (uint32_t triangle = 0; triangle < result->triangleCount; triangle++) {
        std::array<ExactPosition, 3> exactVertices {};
        std::array<QuantizedPosition, 3> quantizedVertices {};
        bool triangleValid = true;
        bool triangleQuantized = true;

        for (uint32_t vertex = 0; vertex < 3; vertex++) {
          const uint32_t index = getIndex(triangle * 3 + vertex);
          if (index >= result->streamVertexCount) {
            triangleValid = false;
            triangleQuantized = false;
            break;
          }

          exactVertices[vertex] = readPositionBits(positions, index);
          triangleQuantized &= quantizePosition(
            positionBitsToFloats(exactVertices[vertex]),
            quantizedVertices[vertex]);
        }

        if (!triangleValid) {
          continue;
        }

        std::sort(exactVertices.begin(), exactVertices.end());
        ExactTriangle exactTriangle {};
        for (uint32_t vertex = 0; vertex < 3; vertex++) {
          std::copy(
            exactVertices[vertex].begin(),
            exactVertices[vertex].end(),
            exactTriangle.begin() + vertex * 3);
        }
        exactTriangles.push_back(exactTriangle);

        if (triangleQuantized) {
          std::sort(quantizedVertices.begin(), quantizedVertices.end());
          QuantizedTriangle quantizedTriangle {};
          for (uint32_t vertex = 0; vertex < 3; vertex++) {
            std::copy(
              quantizedVertices[vertex].begin(),
              quantizedVertices[vertex].end(),
              quantizedTriangle.begin() + vertex * 3);
          }
          quantizedTriangles.push_back(quantizedTriangle);
        } else {
          allTrianglePositionsQuantized = false;
        }
      }

      result->triangleCanonicalizationValid = result->invalidIndexCount == 0
        && exactTriangles.size() == result->triangleCount
        && !exactTriangles.empty();
      if (result->triangleCanonicalizationValid) {
        std::sort(exactTriangles.begin(), exactTriangles.end());
        result->canonicalTriangleHashExact = XXH3_64bits(
          exactTriangles.data(),
          exactTriangles.size() * sizeof(exactTriangles[0]));
      }
      if (result->triangleCanonicalizationValid && allTrianglePositionsQuantized
          && quantizedTriangles.size() == exactTriangles.size()) {
        std::sort(quantizedTriangles.begin(), quantizedTriangles.end());
        result->canonicalTriangleHashQuantized = XXH3_64bits(
          quantizedTriangles.data(),
          quantizedTriangles.size() * sizeof(quantizedTriangles[0]));
      }

      return result;
    }
  }

  bool getVertexRegion(const RasterBuffer& buffer, const size_t vertexCount, HashQuery& outResult) {
    ScopedCpuProfileZone();

    if (!buffer.defined())
      return false;

    outResult.pBase = (uint8_t*) buffer.mapPtr(buffer.offsetFromSlice());
    outResult.elementSize = imageFormatInfo(buffer.vertexFormat())->elementSize;
    outResult.stride = buffer.stride();
    outResult.size = outResult.stride * vertexCount;
    // Make sure we hold on to this reference while the hashing is in flight
    outResult.ref = buffer.buffer().ptr();
    assert(outResult.ref);
    return true;
  }

  // Sorts and deduplicates a set of integers, storing the result in a vector
  template<typename T>
  void deduplicateSortIndices(const void* pIndexData, const size_t indexCount, const uint32_t maxIndexValue, std::vector<T>& uniqueIndicesOut) {
    // TODO (REMIX-657): Implement optimized variant of this function
    // We know there will be at most, this many unique indices
    const uint32_t indexRange = maxIndexValue + 1;

    // Initialize all to 0
    uniqueIndicesOut.resize(indexRange, (T)0);

    // Use memory as a bin table for index data
    for (uint32_t i = 0; i < indexCount; i++) {
      const T& index = ((T*) pIndexData)[i];
      assert(index <= maxIndexValue);
      uniqueIndicesOut[index] = 1;
    }

    // Repopulate the bins with contiguous index values
    uint32_t uniqueIndexCount = 0;
    for (uint32_t i = 0; i < indexRange; i++) {
      if (uniqueIndicesOut[i])
        uniqueIndicesOut[uniqueIndexCount++] = i;
    }

    // Remove any unused entries
    uniqueIndicesOut.resize(uniqueIndexCount);
  }

  template<typename T>
  void hashGeometryData(const size_t indexCount, const uint32_t maxIndexValue, const void* pIndexData,
                        DxvkBuffer* indexBufferRef, const HashQuery vertexRegions[VertexRegions::Count],
                        VkPrimitiveTopology topology, bool collectDebugData, GeometryHashes& hashesOut) {
    ScopedCpuProfileZone();

    const HashRule& globalHashRule = RtxOptions::geometryHashGenerationRule();

    // TODO (REMIX-658): Improve this by reducing allocation overhead of vector
    std::vector<T> uniqueIndices(0);
    if constexpr (!std::is_same<T, NoIndices>::value) {
      assert((indexCount > 0 && indexBufferRef));
      deduplicateSortIndices(pIndexData, indexCount, maxIndexValue, uniqueIndices);

      if (globalHashRule.test(HashComponents::Indices)) {
        hashesOut[HashComponents::Indices] = hashContiguousMemory(pIndexData, indexCount * sizeof(T));
      }

      // TODO (REMIX-656): Remove this once we can transition content to new hash
      if (globalHashRule.test(HashComponents::LegacyIndices)) {
        hashesOut[HashComponents::LegacyIndices] = hashIndicesLegacy<T>(pIndexData, indexCount);
      }

    }

    // Do vertex based rules
    for (uint32_t i = 0; i < (uint32_t) HashComponents::Count; i++) {
      const HashComponents& component = (HashComponents) i;

      if (globalHashRule.test(component) && componentToRegionMap.count(component) > 0) {
        const VertexRegions::Type region = componentToRegionMap.at(component);
        hashesOut[component] = hashVertexRegionIndexed(vertexRegions[(uint32_t)region], uniqueIndices);
      }
    }

    // TODO (REMIX-656): Remove this once we can transition content to new hash
    if (globalHashRule.test(HashComponents::LegacyPositions0) || globalHashRule.test(HashComponents::LegacyPositions1)) {
      hashRegionLegacy(vertexRegions[VertexRegions::Position], hashesOut[HashComponents::LegacyPositions0], hashesOut[HashComponents::LegacyPositions1]);
    }

    if (collectDebugData) {
      hashesOut.debugData = buildGeometryHashDebugData(
        indexCount,
        pIndexData,
        vertexRegions[VertexRegions::Position],
        uniqueIndices,
        topology);
    }

    if constexpr (!std::is_same<T, NoIndices>::value) {
      // Release this memory back to the staging allocator only after optional
      // canonical diagnostics have consumed the raw index stream.
      indexBufferRef->release(DxvkAccess::Read);
      indexBufferRef->decRef();
    }

    // Release this memory back to the staging allocator
    for (uint32_t i = 0; i < VertexRegions::Count; i++) {
      const HashQuery& region = vertexRegions[i];
      if (region.size == 0)
        continue;

      if (region.ref) {
        region.ref->release(DxvkAccess::Read);
        region.ref->decRef();
      }
    }
  }

  Future<GeometryHashes> D3D9Rtx::computeHash(const RasterGeometry& geoData, const uint32_t maxIndexValue) {
    ScopedCpuProfileZone();

    const uint32_t indexCount = geoData.indexCount;
    const uint32_t vertexCount = geoData.vertexCount;

    HashQuery vertexRegions[VertexRegions::Count];
    memset(&vertexRegions[0], 0, sizeof(vertexRegions));

    if (!getVertexRegion(geoData.positionBuffer, vertexCount, vertexRegions[VertexRegions::Position]))
      return Future<GeometryHashes>(); //invalid

    // Acquire prevents the staging allocator from re-using this memory
    vertexRegions[VertexRegions::Position].ref->acquire(DxvkAccess::Read);
    vertexRegions[VertexRegions::Position].ref->incRef();

    if (getVertexRegion(geoData.texcoordBuffer, vertexCount, vertexRegions[VertexRegions::Texcoord])) {
      vertexRegions[VertexRegions::Texcoord].ref->acquire(DxvkAccess::Read);
      vertexRegions[VertexRegions::Texcoord].ref->incRef();
    }

    // Make sure we hold a ref to the index buffer while hashing.
    const Rc<DxvkBuffer> indexBufferRef = geoData.indexBuffer.buffer();
    if (indexBufferRef.ptr()) {
      indexBufferRef->acquire(DxvkAccess::Read);
      indexBufferRef->incRef();
    }
    const void* pIndexData = geoData.indexBuffer.defined() ? geoData.indexBuffer.mapPtr(0) : nullptr;
    const size_t indexStride = geoData.indexBuffer.stride();
    const size_t indexDataSize = indexCount * indexStride;

    // Assume the GPU changed the data via shaders, include the constant buffer data in hash
    XXH64_hash_t vertexShaderHash = kEmptyHash;
    if (m_parent->UseProgrammableVS() && useVertexCapture()) {
      if (RtxOptions::geometryHashGenerationRule().test(HashComponents::GeometryDescriptor)) {
        const D3D9ConstantSets& cb = m_parent->m_consts[DxsoProgramTypes::VertexShader];
        auto& shaderByteCode = d3d9State().vertexShader->GetCommonShader()->GetBytecode();
        vertexShaderHash = XXH3_64bits(shaderByteCode.data(), shaderByteCode.size());
        vertexShaderHash = XXH3_64bits_withSeed(&d3d9State().vsConsts.fConsts[0], cb.meta.maxConstIndexF * sizeof(float) * 4, vertexShaderHash);
        vertexShaderHash = XXH3_64bits_withSeed(&d3d9State().vsConsts.iConsts[0], cb.meta.maxConstIndexI * sizeof(int) * 4, vertexShaderHash);
        vertexShaderHash = XXH3_64bits_withSeed(&d3d9State().vsConsts.bConsts[0], cb.meta.maxConstIndexB * sizeof(uint32_t)/32, vertexShaderHash);
      }
    }

    // Calculate this based on the RasterGeometry input data
    XXH64_hash_t geometryDescriptorHash = kEmptyHash;
    if (RtxOptions::geometryHashGenerationRule().test(HashComponents::GeometryDescriptor)) {
      geometryDescriptorHash = hashGeometryDescriptor(geoData.indexCount, 
                                                      geoData.vertexCount, 
                                                      geoData.indexBuffer.indexType(), 
                                                      geoData.topology);
    }

    // Calculate this based on the RasterGeometry input data
    XXH64_hash_t vertexLayoutHash = kEmptyHash;
    if (RtxOptions::geometryHashGenerationRule().test(HashComponents::VertexLayout)) {
      vertexLayoutHash = hashVertexLayout(geoData);
    }

    const bool collectDebugData = RtxOptions::enableInstrumentation()
      && m_parent->GetDXVKDevice()->getCommon()->getResources()
        .getRaytracingOutput().m_primaryObjectPicking.isValid();

    return m_pGeometryWorkers->Schedule([vertexRegions, indexBufferRef = indexBufferRef.ptr(),
                                 pIndexData, indexStride, indexDataSize, indexCount,
                                 maxIndexValue, vertexShaderHash, geometryDescriptorHash,
                                 vertexLayoutHash, topology = geoData.topology,
                                 collectDebugData]() -> GeometryHashes {
      ScopedCpuProfileZone();

      GeometryHashes hashes;

      // Finalize the descriptor hash
      hashes[HashComponents::GeometryDescriptor] = geometryDescriptorHash;
      hashes[HashComponents::VertexLayout] = vertexLayoutHash;
      hashes[HashComponents::VertexShader] = vertexShaderHash;

      // Index hash
      switch (indexStride) {
      case 2:
        hashGeometryData<uint16_t>(indexCount, maxIndexValue, pIndexData, indexBufferRef, vertexRegions, topology, collectDebugData, hashes);
        break;
      case 4:
        hashGeometryData<uint32_t>(indexCount, maxIndexValue, pIndexData, indexBufferRef, vertexRegions, topology, collectDebugData, hashes);
        break;
      default:
        hashGeometryData<NoIndices>(indexCount, maxIndexValue, pIndexData, indexBufferRef, vertexRegions, topology, collectDebugData, hashes);
        break;
      }

      assert(hashes[HashComponents::VertexPosition] != kEmptyHash);

      hashes.precombine();

      return hashes;
    });
  }

  Future<AxisAlignedBoundingBox> D3D9Rtx::computeAxisAlignedBoundingBox(const RasterGeometry& geoData) {
    ScopedCpuProfileZone();

    if (!RtxOptions::needsMeshBoundingBox()) {
      return Future<AxisAlignedBoundingBox>();
    }

    const void* pVertexData = geoData.positionBuffer.mapPtr((size_t)geoData.positionBuffer.offsetFromSlice());
    const uint32_t vertexCount = geoData.vertexCount;
    const size_t vertexStride = geoData.positionBuffer.stride();

    if (pVertexData == nullptr) {
      return Future<AxisAlignedBoundingBox>();
    }

    auto vertexBuffer = geoData.positionBuffer.buffer().ptr();
    vertexBuffer->incRef();

    return m_pGeometryWorkers->Schedule([pVertexData, vertexCount, vertexStride, vertexBuffer]()->AxisAlignedBoundingBox {
      ScopedCpuProfileZone();

#if defined(_M_ARM64) || defined(_M_ARM64EC)
      float32x4_t minPos = vdupq_n_f32(FLT_MAX);
      float32x4_t maxPos = vdupq_n_f32(-FLT_MAX);

      const uint8_t* pVertex = static_cast<const uint8_t*>(pVertexData);
      for (uint32_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
        const Vector3* const pVertexPos = reinterpret_cast<const Vector3* const>(pVertex);
        float32x4_t vertexPos;
        vertexPos.n128_f32[0] = pVertexPos->x;
        vertexPos.n128_f32[1] = pVertexPos->y;
        vertexPos.n128_f32[2] = pVertexPos->z;

        minPos = vminq_f32(minPos, vertexPos);
        maxPos = vmaxq_f32(maxPos, vertexPos);

        pVertex += vertexStride;
      }

      AxisAlignedBoundingBox boundingBox {
        Vector3{ vgetq_lane_f32(minPos, 0), vgetq_lane_f32(minPos, 1), vgetq_lane_f32(minPos, 2) },
        Vector3{ vgetq_lane_f32(maxPos, 0), vgetq_lane_f32(maxPos, 1), vgetq_lane_f32(maxPos, 2) }
      };
#else
      __m128 minPos = _mm_set_ps1(FLT_MAX);
      __m128 maxPos = _mm_set_ps1(-FLT_MAX);

      const uint8_t* pVertex = static_cast<const uint8_t*>(pVertexData);
      for (uint32_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
        const Vector3* const pVertexPos = reinterpret_cast<const Vector3* const>(pVertex);
        __m128 vertexPos = _mm_set_ps(0.0f, pVertexPos->z, pVertexPos->y, pVertexPos->x);
        minPos = _mm_min_ps(minPos, vertexPos);
        maxPos = _mm_max_ps(maxPos, vertexPos);

        pVertex += vertexStride;
      }

      AxisAlignedBoundingBox boundingBox{
        Vector3{ minPos.m128_f32[0], minPos.m128_f32[1], minPos.m128_f32[2] },
        Vector3{ maxPos.m128_f32[0], maxPos.m128_f32[1], maxPos.m128_f32[2] }
      };
#endif

      vertexBuffer->decRef();

      return boundingBox;
    });
  }
}
