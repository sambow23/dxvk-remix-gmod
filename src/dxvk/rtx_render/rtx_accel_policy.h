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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dxvk {

  enum class BlasLifecycleState : uint8_t {
    Updateable,
    Static,
    Compacted,
  };

  enum class BlasCompactionState : uint8_t {
    None,
    AwaitingQuery,
    QueryPending,
    Ready,
    Complete,
    Rejected,
  };

  struct BlasLifecycle {
    BlasLifecycleState state = BlasLifecycleState::Updateable;
    BlasCompactionState compactionState = BlasCompactionState::None;
    uint32_t stableFrameCount = 0;
    uint32_t refitCount = 0;
    uint32_t lastObservedFrame = std::numeric_limits<uint32_t>::max();
    uint32_t lastBuildFrame = std::numeric_limits<uint32_t>::max();
    uint64_t buildGeneration = 0;
    uint64_t uncompactedSize = 0;
    uint64_t compactedSize = 0;
    bool bakeEligible = false;

    void observe(uint32_t frameId, bool geometryUpdated, bool eligible) {
      bakeEligible = eligible;

      if (lastObservedFrame == frameId) {
        if (!eligible || geometryUpdated) {
          stableFrameCount = 0;
        }
        return;
      }

      lastObservedFrame = frameId;
      if (!eligible || geometryUpdated) {
        stableFrameCount = 0;
      } else if (state == BlasLifecycleState::Updateable) {
        stableFrameCount = std::min(stableFrameCount + 1, std::numeric_limits<uint32_t>::max());
      }
    }

    bool isBakeReady(uint32_t minStableFrames) const {
      return bakeEligible
          && state == BlasLifecycleState::Updateable
          && stableFrameCount >= minStableFrames;
    }

    bool needsQualityRebuild(uint32_t maxRefits) const {
      return state == BlasLifecycleState::Updateable
          && maxRefits != 0
          && refitCount >= maxRefits;
    }

    void markBuilt(BlasLifecycleState newState, uint32_t frameId, uint64_t size) {
      state = newState;
      refitCount = 0;
      lastBuildFrame = frameId;
      ++buildGeneration;
      uncompactedSize = size;
      compactedSize = 0;
      compactionState = newState == BlasLifecycleState::Static
        ? BlasCompactionState::AwaitingQuery
        : BlasCompactionState::None;
    }

    void markRefit() {
      refitCount = std::min(refitCount + 1, std::numeric_limits<uint32_t>::max());
    }

    bool isCurrentCompactionQuery(uint64_t generation) const {
      return state == BlasLifecycleState::Static
          && compactionState == BlasCompactionState::QueryPending
          && buildGeneration == generation;
    }

    bool acceptCompactionSize(uint64_t generation, uint64_t size, uint32_t minSavingsPercent) {
      if (!isCurrentCompactionQuery(generation)) {
        return false;
      }

      if (size == 0 || size >= uncompactedSize) {
        compactedSize = 0;
        compactionState = BlasCompactionState::Rejected;
        return false;
      }

      compactedSize = size;
      const uint64_t savedBytes = uncompactedSize - compactedSize;
      const uint64_t requiredSavings = (uncompactedSize * std::min(minSavingsPercent, 100u) + 99u) / 100u;
      compactionState = savedBytes >= requiredSavings
        ? BlasCompactionState::Ready
        : BlasCompactionState::Rejected;
      return compactionState == BlasCompactionState::Ready;
    }

    void markCompacted() {
      state = BlasLifecycleState::Compacted;
      compactionState = BlasCompactionState::Complete;
    }

    void cancelCompaction() {
      ++buildGeneration;
      compactedSize = 0;
      compactionState = BlasCompactionState::None;
    }
  };

  struct SpatialBlasClusterItem {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t primitiveCount = 0;
    uint32_t geometryCount = 0;
  };

  inline bool isMergedBlasBakeEligible(uint32_t boneCount, bool hasStableTransform) {
    return boneCount == 0 && hasStableTransform;
  }

  enum class MergedBlasChange : uint32_t {
    NewAllocation = 1u << 0,
    Reassignment = 1u << 1,
    Layout = 1u << 2,
    Topology = 1u << 3,
    Vertex = 1u << 4,
    Bone = 1u << 5,
    Transform = 1u << 6,
    Unknown = 1u << 7,
  };

  struct MergedBlasContentSignature {
    uint64_t content = 0;
    uint64_t layout = 0;
    uint64_t topology = 0;
    uint64_t vertex = 0;
    uint64_t bone = 0;
    uint64_t transform = 0;
  };

  inline bool hasSameMergedBlasBuildContent(
      const MergedBlasContentSignature& previous,
      const MergedBlasContentSignature& current) {
    return current.content != 0
        && previous.content == current.content
        && previous.topology == current.topology;
  }

  inline bool canRefitMergedBlas(
      BlasLifecycleState state,
      uint64_t previousTopology,
      uint64_t currentTopology,
      const std::vector<uint32_t>& previousPrimitiveCounts,
      const std::vector<uint32_t>& currentPrimitiveCounts) {
    return state == BlasLifecycleState::Updateable
        && previousTopology == currentTopology
        && previousPrimitiveCounts == currentPrimitiveCounts;
  }

  constexpr uint32_t mergedBlasChangeBit(MergedBlasChange change) {
    return static_cast<uint32_t>(change);
  }

  inline bool hasMergedBlasChange(uint32_t changes, MergedBlasChange change) {
    return (changes & mergedBlasChangeBit(change)) != 0;
  }

  inline uint32_t classifyMergedBlasChanges(
      bool isNewAllocation,
      bool reusedPreviousBlas,
      const MergedBlasContentSignature& previous,
      const MergedBlasContentSignature& current) {
    if (isNewAllocation) {
      return mergedBlasChangeBit(MergedBlasChange::NewAllocation);
    }

    uint32_t changes = reusedPreviousBlas
      ? 0
      : mergedBlasChangeBit(MergedBlasChange::Reassignment);
    if (previous.layout != current.layout) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Layout);
    }
    if (previous.topology != current.topology) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Topology);
    }
    if (previous.vertex != current.vertex) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Vertex);
    }
    if (previous.bone != current.bone) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Bone);
    }
    if (previous.transform != current.transform) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Transform);
    }

    constexpr uint32_t kKnownContentChanges =
      mergedBlasChangeBit(MergedBlasChange::Layout)
      | mergedBlasChangeBit(MergedBlasChange::Topology)
      | mergedBlasChangeBit(MergedBlasChange::Vertex)
      | mergedBlasChangeBit(MergedBlasChange::Bone)
      | mergedBlasChangeBit(MergedBlasChange::Transform);
    if (previous.content != current.content && (changes & kKnownContentChanges) == 0) {
      changes |= mergedBlasChangeBit(MergedBlasChange::Unknown);
    }
    return changes;
  }

  struct SpatialBlasClusterRange {
    size_t first = 0;
    size_t count = 0;
    uint64_t primitiveCount = 0;
    uint64_t geometryCount = 0;
  };

  struct SpatialBlasClusterPlan {
    std::vector<size_t> orderedItemIndices;
    std::vector<SpatialBlasClusterRange> ranges;
  };

  namespace detail {

    inline uint32_t expandMortonBits(uint32_t value) {
      value &= 0x000003ffu;
      value = (value | value << 16) & 0x030000ffu;
      value = (value | value << 8) & 0x0300f00fu;
      value = (value | value << 4) & 0x030c30c3u;
      value = (value | value << 2) & 0x09249249u;
      return value;
    }

    inline uint32_t quantizeSpatialCoordinate(float value, float minValue, float maxValue) {
      if (!std::isfinite(value) || !(maxValue > minValue)) {
        return 0;
      }

      constexpr float kMortonScale = 1023.0f;
      const float normalized = std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
      return static_cast<uint32_t>(normalized * kMortonScale + 0.5f);
    }

    inline uint32_t calculateMortonCode(
        const SpatialBlasClusterItem& item,
        const std::array<float, 3>& minPosition,
        const std::array<float, 3>& maxPosition) {
      const uint32_t x = quantizeSpatialCoordinate(item.x, minPosition[0], maxPosition[0]);
      const uint32_t y = quantizeSpatialCoordinate(item.y, minPosition[1], maxPosition[1]);
      const uint32_t z = quantizeSpatialCoordinate(item.z, minPosition[2], maxPosition[2]);
      return expandMortonBits(x) | expandMortonBits(y) << 1 | expandMortonBits(z) << 2;
    }

  }

  inline SpatialBlasClusterPlan planSpatialBlasClusters(
      const std::vector<SpatialBlasClusterItem>& items,
      uint32_t maxPrimitiveCount,
      uint32_t maxGeometryCount) {
    SpatialBlasClusterPlan plan;
    if (items.empty()) {
      return plan;
    }

    std::array<float, 3> minPosition {
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
    };
    std::array<float, 3> maxPosition {
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
    };
    std::array<bool, 3> hasFiniteCoordinate {};

    for (const SpatialBlasClusterItem& item : items) {
      const std::array<float, 3> position { item.x, item.y, item.z };
      for (size_t axis = 0; axis < position.size(); ++axis) {
        if (std::isfinite(position[axis])) {
          minPosition[axis] = std::min(minPosition[axis], position[axis]);
          maxPosition[axis] = std::max(maxPosition[axis], position[axis]);
          hasFiniteCoordinate[axis] = true;
        }
      }
    }

    for (size_t axis = 0; axis < minPosition.size(); ++axis) {
      if (!hasFiniteCoordinate[axis]) {
        minPosition[axis] = 0.0f;
        maxPosition[axis] = 0.0f;
      }
    }

    struct MortonItem {
      uint32_t code;
      size_t itemIndex;
    };
    std::vector<MortonItem> mortonItems;
    mortonItems.reserve(items.size());
    for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
      mortonItems.push_back({
        detail::calculateMortonCode(items[itemIndex], minPosition, maxPosition),
        itemIndex,
      });
    }

    std::sort(mortonItems.begin(), mortonItems.end(), [](const MortonItem& a, const MortonItem& b) {
      return a.code != b.code ? a.code < b.code : a.itemIndex < b.itemIndex;
    });

    plan.orderedItemIndices.reserve(items.size());
    SpatialBlasClusterRange range;
    for (const MortonItem& mortonItem : mortonItems) {
      const SpatialBlasClusterItem& item = items[mortonItem.itemIndex];
      const bool exceedsPrimitiveLimit = range.count != 0
        && maxPrimitiveCount != 0
        && range.primitiveCount + item.primitiveCount > maxPrimitiveCount;
      const bool exceedsGeometryLimit = range.count != 0
        && maxGeometryCount != 0
        && range.geometryCount + item.geometryCount > maxGeometryCount;

      if (exceedsPrimitiveLimit || exceedsGeometryLimit) {
        plan.ranges.push_back(range);
        range = {};
        range.first = plan.orderedItemIndices.size();
      }

      plan.orderedItemIndices.push_back(mortonItem.itemIndex);
      ++range.count;
      range.primitiveCount += item.primitiveCount;
      range.geometryCount += item.geometryCount;
    }

    if (range.count != 0) {
      plan.ranges.push_back(range);
    }
    return plan;
  }

}
