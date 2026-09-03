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

#include <iostream>

#include "../../../src/dxvk/rtx_render/rtx_accel_policy.h"

namespace {

  bool expect(bool condition, const char* message) {
    if (!condition) {
      std::cerr << message << '\n';
    }
    return condition;
  }

}

int main() {
  using namespace dxvk;

  BlasLifecycle lifecycle;
  lifecycle.observe(1, true, true);
  lifecycle.observe(2, false, true);
  lifecycle.observe(3, false, true);
  if (!expect(lifecycle.isBakeReady(2), "Stable eligible BLAS did not become bake-ready")) {
    return -1;
  }

  lifecycle.observe(3, true, true);
  if (!expect(lifecycle.stableFrameCount == 0, "Same-frame geometry update did not reset stability")) {
    return -1;
  }

  BlasLifecycle ineligibleLifecycle;
  ineligibleLifecycle.observe(1, false, true);
  ineligibleLifecycle.observe(2, false, true);
  ineligibleLifecycle.observe(3, false, false);
  if (!expect(ineligibleLifecycle.stableFrameCount == 0
           && !ineligibleLifecycle.isBakeReady(2),
              "Ineligible BLAS retained bake-ready stability")) {
    return -1;
  }

  if (!expect(isMergedBlasBakeEligible(0, true)
           && !isMergedBlasBakeEligible(0, false)
           && !isMergedBlasBakeEligible(1, true),
              "Merged BLAS eligibility admitted moving or skinned geometry")) {
    return -1;
  }

  const MergedBlasContentSignature previousSignature { 1, 2, 3, 4, 5, 6 };
  const MergedBlasContentSignature currentSignature { 7, 8, 3, 9, 5, 10 };
  if (!expect(hasSameMergedBlasBuildContent(
                previousSignature,
                MergedBlasContentSignature { 1, 8, 3, 4, 5, 6 })
           && !hasSameMergedBlasBuildContent(previousSignature, currentSignature)
           && !hasSameMergedBlasBuildContent(
                previousSignature,
                MergedBlasContentSignature { 1, 2, 7, 4, 5, 6 }),
              "Merged BLAS content recovery used unstable identity or mismatched build data")) {
    return -1;
  }

  const std::vector<uint32_t> primitiveCounts { 10, 20 };
  if (!expect(canRefitMergedBlas(
                BlasLifecycleState::Updateable, 3, 3, primitiveCounts, primitiveCounts)
           && !canRefitMergedBlas(
                BlasLifecycleState::Compacted, 3, 3, primitiveCounts, primitiveCounts)
           && !canRefitMergedBlas(
                BlasLifecycleState::Updateable, 3, 4, primitiveCounts, primitiveCounts)
           && !canRefitMergedBlas(
                BlasLifecycleState::Updateable, 3, 3, primitiveCounts, { 10, 21 }),
              "Merged BLAS refit compatibility accepted incompatible pooled storage")) {
    return -1;
  }

  const uint32_t changes = classifyMergedBlasChanges(
    false, false, previousSignature, currentSignature);
  if (!expect(hasMergedBlasChange(changes, MergedBlasChange::Reassignment)
           && hasMergedBlasChange(changes, MergedBlasChange::Layout)
           && !hasMergedBlasChange(changes, MergedBlasChange::Topology)
           && hasMergedBlasChange(changes, MergedBlasChange::Vertex)
           && !hasMergedBlasChange(changes, MergedBlasChange::Bone)
           && hasMergedBlasChange(changes, MergedBlasChange::Transform)
           && !hasMergedBlasChange(changes, MergedBlasChange::Unknown),
              "Merged BLAS change classification reported the wrong causes")) {
    return -1;
  }

  const uint32_t unknownChange = classifyMergedBlasChanges(
    false,
    true,
    previousSignature,
    MergedBlasContentSignature { 7, 2, 3, 4, 5, 6 });
  if (!expect(hasMergedBlasChange(unknownChange, MergedBlasChange::Unknown),
              "Unclassified merged BLAS content change was not reported")) {
    return -1;
  }

  const uint32_t allocationChange = classifyMergedBlasChanges(
    true, false, previousSignature, currentSignature);
  if (!expect(allocationChange == mergedBlasChangeBit(MergedBlasChange::NewAllocation),
              "New merged BLAS allocation reported stale component changes")) {
    return -1;
  }

  lifecycle.markBuilt(BlasLifecycleState::Updateable, 3, 1000);
  lifecycle.markRefit();
  lifecycle.markRefit();
  if (!expect(lifecycle.needsQualityRebuild(2), "Refit limit did not request a quality rebuild")) {
    return -1;
  }

  lifecycle.markBuilt(BlasLifecycleState::Static, 4, 1000);
  const uint64_t staticGeneration = lifecycle.buildGeneration;
  lifecycle.compactionState = BlasCompactionState::QueryPending;
  if (!expect(lifecycle.acceptCompactionSize(staticGeneration, 899, 10),
              "Profitable compaction result was rejected")) {
    return -1;
  }

  lifecycle.markCompacted();
  if (!expect(lifecycle.state == BlasLifecycleState::Compacted,
              "Compaction did not transition the BLAS to compacted")) {
    return -1;
  }

  lifecycle.markBuilt(BlasLifecycleState::Static, 5, 1000);
  const uint64_t staleGeneration = lifecycle.buildGeneration;
  lifecycle.compactionState = BlasCompactionState::QueryPending;
  lifecycle.markBuilt(BlasLifecycleState::Static, 6, 1000);
  if (!expect(!lifecycle.acceptCompactionSize(staleGeneration, 500, 10),
              "Stale compaction result was accepted")) {
    return -1;
  }

  const uint64_t currentGeneration = lifecycle.buildGeneration;
  lifecycle.compactionState = BlasCompactionState::QueryPending;
  if (!expect(!lifecycle.acceptCompactionSize(currentGeneration, 901, 10)
           && lifecycle.compactionState == BlasCompactionState::Rejected,
              "Unprofitable compaction result was accepted")) {
    return -1;
  }

  const std::vector<SpatialBlasClusterItem> spatialItems {
    { 0.0f, 0.0f, 0.0f, 6, 1 },
    { 100.0f, 0.0f, 0.0f, 6, 1 },
    { 1.0f, 0.0f, 0.0f, 4, 1 },
    { 101.0f, 0.0f, 0.0f, 4, 1 },
  };
  const SpatialBlasClusterPlan spatialPlan = planSpatialBlasClusters(spatialItems, 10, 0);
  if (!expect(spatialPlan.orderedItemIndices == std::vector<size_t>({ 0, 2, 1, 3 }),
              "Spatial cluster order did not preserve locality")) {
    return -1;
  }
  if (!expect(spatialPlan.ranges.size() == 2
           && spatialPlan.ranges[0].count == 2
           && spatialPlan.ranges[0].primitiveCount == 10
           && spatialPlan.ranges[1].count == 2
           && spatialPlan.ranges[1].primitiveCount == 10,
              "Spatial clusters did not respect the primitive limit")) {
    return -1;
  }

  const std::vector<SpatialBlasClusterItem> geometryLimitedItems {
    { 0.0f, 0.0f, 0.0f, 1, 2 },
    { 1.0f, 0.0f, 0.0f, 1, 2 },
    { 2.0f, 0.0f, 0.0f, 1, 1 },
  };
  const SpatialBlasClusterPlan geometryLimitedPlan = planSpatialBlasClusters(geometryLimitedItems, 0, 3);
  if (!expect(geometryLimitedPlan.ranges.size() == 2
           && geometryLimitedPlan.ranges[0].geometryCount == 2
           && geometryLimitedPlan.ranges[1].geometryCount == 3,
              "Spatial clusters did not respect the geometry limit")) {
    return -1;
  }

  const std::vector<SpatialBlasClusterItem> tiedItems {
    { 1.0f, 1.0f, 1.0f, 100, 1 },
    { 1.0f, 1.0f, 1.0f, 1, 1 },
  };
  const SpatialBlasClusterPlan tiedPlan = planSpatialBlasClusters(tiedItems, 10, 0);
  if (!expect(tiedPlan.orderedItemIndices == std::vector<size_t>({ 0, 1 })
           && tiedPlan.ranges.size() == 2,
              "Spatial cluster ties or oversized items were not handled deterministically")) {
    return -1;
  }

  return 0;
}
