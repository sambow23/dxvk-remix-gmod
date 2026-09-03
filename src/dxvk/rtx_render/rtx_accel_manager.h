/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <array>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx_common_object.h"
#include "rtx_gpu_crash_recorder.h"
#include "rtx_option.h"
#include "rtx_staging.h"
#include "rtx_point_instancer_system.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "../util/util_struct_hash.h"

namespace dxvk 
{
class DxvkContext;
class DxvkDevice;
class ResourceCache;
class CameraManager;
class OpacityMicromapManager;

// AccelManager is responsible for maintaining the acceleration structures (BLAS and TLAS)
class AccelManager : public CommonDeviceObject {
  friend class ImGUI;

  RTX_OPTION("rtx.accelerationStructure", bool, enableBlasBaking, false,
             "Enables experimental runtime baking of stable dedicated and merged BLASes for faster tracing.");
  RTX_OPTION("rtx.accelerationStructure", bool, enableSpatialBlasClustering, true,
             "Spatially partitions merged geometry into bounded BLASes to reduce overlap during ray traversal.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, spatialBlasClusterMaxPrimitives, 50000,
             "Maximum aggregate primitive count in a spatially clustered merged BLAS. 0: Unlimited.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, spatialBlasClusterMaxGeometries, 32,
             "Maximum geometry count in a spatially clustered merged BLAS. 0: Unlimited.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, staticBlasMinStableFrames, 60,
             "Number of active unchanged frames before an eligible BLAS is rebuilt for fast tracing.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, maxBlasRefits, 120,
             "Maximum number of BLAS refits before an optional full rebuild restores traversal quality. 0: Disabled.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, maxMaintenanceOperationsPerFrame, 1,
             "Maximum optional BLAS promotion, quality rebuild, or compaction operations per frame. 0: Unlimited.");
  RTX_OPTION("rtx.accelerationStructure", uint32_t, blasCompactionMinSavingsPercent, 10,
             "Minimum measured memory savings required before compacting a baked BLAS, in percent.");

  class BlasBucket {
  public:
    std::vector<VkAccelerationStructureGeometryKHR> geometries {};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges {};
    std::vector<RtInstance*> originalInstances {};
    std::vector<uint32_t> primitiveCounts {};
    std::vector<uint32_t> instanceBillboardIndices {};  // Billboard index within an instance's billboard array
    std::vector<uint32_t> indexOffsets {};              // Index offsets within geometry
    uint8_t instanceMask = 0;
    uint32_t instanceShaderBindingTableRecordOffset = 0;
    uint32_t customIndexFlags = 0;
    VkGeometryInstanceFlagsKHR instanceFlags = 0;
    bool usesUnorderedApproximations = false;
    uint32_t reorderedSurfacesOffset = UINT32_MAX;
    bool hasOmmInstances = false;
    bool hasSssInstances = false;
    bool bakeEligible = true;

    // The PooledBlas assigned to this bucket by createBlasBuffersAndInstances.
    // Stored here so the per-bucket cache can capture it after buildBlases.
    PooledBlas* assignedBlas = nullptr;
    PooledBlas* previousBlas = nullptr;
    
    // Tries to add a geometry instance to the bucket. The addition is successful if either:
    //   a) the bucket is empty,
    //   b) the instance has the same mask etc. as all other instances in the bucket.
    bool tryAddInstance(RtInstance* instance);
  };

  // Key for O(1) bucket lookup in the merged-BLAS path.
  // Two instances can share a merged BLAS bucket iff they have identical keys.
  struct BlasBucketKey {
    uint32_t instanceShaderBindingTableRecordOffset = 0;
    uint32_t customIndexFlags = 0;
    VkGeometryInstanceFlagsKHR instanceFlags = 0;
    uint8_t instanceMask = 0;
    bool usesUnorderedApproximations = false;
    bool isSubsurface = false;
    bool bakeEligible = false;

    bool operator==(const BlasBucketKey& other) const {
      return instanceMask == other.instanceMask &&
             instanceShaderBindingTableRecordOffset == other.instanceShaderBindingTableRecordOffset &&
             customIndexFlags == other.customIndexFlags &&
             instanceFlags == other.instanceFlags &&
             usesUnorderedApproximations == other.usesUnorderedApproximations &&
             isSubsurface == other.isSubsurface &&
             bakeEligible == other.bakeEligible;
    }
  };

  struct BlasBucketKeyHash {
    size_t operator()(const BlasBucketKey& k) const {
      return static_cast<size_t>(hashStructByMemory<BlasBucketKey,
          &BlasBucketKey::instanceShaderBindingTableRecordOffset,
          &BlasBucketKey::customIndexFlags,
          &BlasBucketKey::instanceFlags,
          &BlasBucketKey::instanceMask,
          &BlasBucketKey::usesUnorderedApproximations,
          &BlasBucketKey::isSubsurface,
          &BlasBucketKey::bakeEligible>(k));
    }
  };

  struct UniqueBlasInstances {
    BlasEntry* blasEntry = nullptr;
    std::vector<RtInstance*> instances;
  };

public:
  AccelManager(AccelManager const&) = delete;
  AccelManager& operator=(AccelManager const&) = delete;

  explicit AccelManager(DxvkDevice* device);
  ~AccelManager() override;

  void onDestroy() override;

  // Returns a GPU buffer containing the surface data for active instances
  const Rc<DxvkBuffer> getSurfaceBuffer() const { return m_surfaceBuffer; }

  const Rc<DxvkBuffer> getSurfaceMappingBuffer() const { return m_surfaceMappingBuffer; }

  const Rc<DxvkBuffer> getCurrentFramePrimitiveIDPrefixSumBuffer() const {
    return m_primitiveIDPrefixSumBuffer;
  }

  const Rc<DxvkBuffer> getLastFramePrimitiveIDPrefixSumBuffer() const {
    return m_primitiveIDPrefixSumBufferLastFrame;
  }

  const Rc<DxvkBuffer> getBillboardsBuffer() const { return m_billboardsBuffer; }

  // Clear all instances currently tracked by manager
  void clear();

  // Clean up instances which are deemed as no longer required
  void garbageCollection();

  // Prepares instance buffers for rendering by the GPU
  void prepareSceneData(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers, InstanceManager& instanceManager);

  // Uploads instances' surface data to the GPU
  void uploadSurfaceData(Rc<DxvkContext> ctx);

  // Merges the RtInstance's into a set of BLAS. Some of the BLAS will contain multiple geometries/instances,
  // and some other BLAS will be dedicated to instances with static geometries.
  void mergeInstancesIntoBlas(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers,
                              const std::vector<TextureRef>& textures, const CameraManager& cameraManager, 
                              InstanceManager& instanceManager, OpacityMicromapManager* opacityMicromapManager);

  // Dispatches GPU compute culling for all PointInstancer batches recorded during
  // mergeInstancesIntoBlas. Must be called after prepareSceneData (placeholders uploaded)
  // and before buildTlas.
  void dispatchPointInstancerCulling(Rc<DxvkContext> ctx, const CameraManager& cameraManager,
                                     const Rc<DxvkBuffer>& surfaceMaterialBuffer);

  void buildTlas(Rc<DxvkContext> ctx);

  void beginFrameGpuTiming(Rc<DxvkContext> ctx);
  void endFrameGpuTiming(Rc<DxvkContext> ctx);
  void beginRayTracingGpuTiming(Rc<DxvkContext> ctx);
  void endRayTracingGpuTiming(Rc<DxvkContext> ctx);

  void dumpCrashState(const char* reason) const { m_gpuCrashRecorder.dump(reason); }

  // Returns the number of live BLAS objects
  static uint32_t getBlasCount();

  uint32_t getSurfaceCount() const { return m_reorderedSurfaces.size(); }
  const std::vector<RtInstance*>& getOrderedInstances() const { return m_reorderedSurfaces; }

  // Returns true if the last mergeInstancesIntoBlas call took the fast-skip
  // path (scene generation unchanged).  When true, m_reorderedSurfaces and
  // all BLAS/surface data are identical to the previous frame, so callers
  // can skip redundant GPU uploads (e.g. surface materials).
  bool wasSceneUnchangedThisFrame() const { return m_sceneUnchangedThisFrame; }

  void removeInstanceFromBucketCache(RtInstance* instance);
  void invalidateOpacityMicromapBindings() { m_ommBindPending = true; }

private:
  static constexpr uint32_t kCompactionQueryCount = 256;

  struct CompactionQuerySlot {
    Rc<PooledBlas> blas;
    uint64_t buildGeneration = 0;
  };

  struct BlasStats {
    uint32_t dedicatedUpdateableCount = 0;
    uint32_t dedicatedStaticCount = 0;
    uint32_t dedicatedCompactedCount = 0;
    uint32_t dedicatedPendingCompactionCount = 0;
    uint32_t mergedUpdateableCount = 0;
    uint32_t mergedStaticCount = 0;
    uint32_t mergedCompactedCount = 0;
    uint32_t mergedPendingCompactionCount = 0;
    uint32_t mergedGeometryCount = 0;
    uint32_t mergedMaxGeometryCount = 0;
    uint32_t mergedBakeEligibleUpdateableCount = 0;
    uint32_t mergedBakeReadyCount = 0;
    uint32_t mergedMinStableFrames = 0;
    uint32_t mergedMaxStableFrames = 0;
    uint32_t mergedZeroStableCount = 0;
    uint32_t mergedOneStableCount = 0;
    uint32_t mergedAdvancingStableCount = 0;
    uint32_t builds = 0;
    uint32_t refits = 0;
    uint32_t qualityRebuilds = 0;
    uint32_t promotions = 0;
    uint32_t compactions = 0;
    uint64_t compactionBytesSaved = 0;
  };

  struct BlasActivityStats {
    uint32_t builds = 0;
    uint32_t refits = 0;
    uint32_t qualityRebuilds = 0;
    uint32_t promotions = 0;
    uint32_t compactions = 0;

    bool hasWork() const {
      return builds != 0
          || refits != 0
          || qualityRebuilds != 0
          || promotions != 0
          || compactions != 0;
    }
  };

  struct MergedBlasChurnStats {
    uint64_t invalidations = 0;
    uint64_t newAllocations = 0;
    uint64_t contentRecoveries = 0;
    uint64_t reassignments = 0;
    uint64_t layoutChanges = 0;
    uint64_t topologyChanges = 0;
    uint64_t vertexChanges = 0;
    uint64_t boneChanges = 0;
    uint64_t transformChanges = 0;
    uint64_t unknownChanges = 0;

    void record(uint32_t changes) {
      ++invalidations;
      newAllocations += hasMergedBlasChange(changes, MergedBlasChange::NewAllocation);
      reassignments += hasMergedBlasChange(changes, MergedBlasChange::Reassignment);
      layoutChanges += hasMergedBlasChange(changes, MergedBlasChange::Layout);
      topologyChanges += hasMergedBlasChange(changes, MergedBlasChange::Topology);
      vertexChanges += hasMergedBlasChange(changes, MergedBlasChange::Vertex);
      boneChanges += hasMergedBlasChange(changes, MergedBlasChange::Bone);
      transformChanges += hasMergedBlasChange(changes, MergedBlasChange::Transform);
      unknownChanges += hasMergedBlasChange(changes, MergedBlasChange::Unknown);
    }
  };

  enum class GpuTimingScope : uint8_t {
    Blas,
    Tlas,
    RayTracing,
    Frame,
  };

  struct PendingGpuTiming {
    GpuTimingScope scope = GpuTimingScope::Blas;
    uint64_t generation = 0;
    Rc<class DxvkGpuQuery> beginQuery;
    Rc<class DxvkGpuQuery> endQuery;
  };

  struct GpuTimingValue {
    float lastMs = 0.0f;
    float lastWorkMs = 0.0f;
    float averageMs = 0.0f;
    bool hasSample = false;
  };

  struct GpuTimingStats {
    GpuTimingValue blas;
    GpuTimingValue tlas;
    GpuTimingValue rayTracing;
    GpuTimingValue frame;
  };

  struct SurfaceInfo {
    uint32_t surfaceMaterialIndex;
    Vector3 worldPosition;
  };

  // Persistent containers to reduce frame to frame reallocations in ::buildParticleSurfaceMapping()
  struct {
    std::vector<AccelManager::SurfaceInfo> surfaceInfoLists[2];   // Two containers for subsequent frames, ping-pong framed to frame
    uint32_t currIndex = 0;
    uint32_t prevIndex = 1;
  } buildParticleSurfaceMappingFuncState;

  // Persistent containers to reduce frame to frame reallocations in ::uploadSurfaceData()
  struct {
    std::vector<unsigned char> surfacesGPUData;
    std::vector<uint32_t> surfaceIndexMapping;
    uint32_t previousFrameSurfaceCount = 0; // Tracks last frame's surface count for mapping coverage
  } uploadSurfaceDataFuncState;

  void buildBlases(Rc<DxvkContext> ctx, DxvkBarrierSet& execBarriers,
                   const CameraManager& cameraManager, OpacityMicromapManager* opacityMicromapManager, const InstanceManager& instanceManager,
                   const std::vector<TextureRef>& textures, const std::vector<RtInstance*>& instances,
                   const std::vector<std::unique_ptr<BlasBucket>>& blasBuckets, 
                   std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& blasToBuild,
                   std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& blasRangesToBuild,
                   const std::vector<VkTransformMatrixKHR>& instanceTransforms,
                   size_t& currentScratchOffset);
  
  void addBlas(RtInstance* instance, BlasEntry* blasEntry, const Matrix4* instanceToObject);
  void addPointInstancerBlas(RtInstance* rtInstance, BlasEntry* blasEntry);

  void createBlasBuffersAndInstances(Rc<DxvkContext> ctx, 
                                     const std::vector<std::unique_ptr<BlasBucket>>& blasBuckets,
                                     std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& blasToBuild,
                                     std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& blasRangesToBuild,
                                     size_t& currentScratchOffset);
  template<Tlas::Type type>
  void internalBuildTlas(Rc<DxvkContext> ctx, size_t& totalScratchSize);

  void buildParticleSurfaceMapping(std::vector<uint32_t>& surfaceIndexMapping);

  bool validateUpdateMode(const VkAccelerationStructureBuildGeometryInfoKHR& oldInfo, const VkAccelerationStructureBuildGeometryInfoKHR& newInfo);

  void beginBlasMaintenance(uint32_t currentFrame);
  bool consumeMaintenanceOperation();
  bool createCompactionQueryPool();
  void destroyCompactionQueryPool();
  void issueCompactionQueries(Rc<DxvkContext> ctx);
  void pollCompactionQueries();
  bool compactBlas(Rc<DxvkContext> ctx, PooledBlas& blas);
  void updateBlasReferences(uint64_t oldReference, uint64_t newReference);
  void ensureBlasStorage(Rc<DxvkContext> ctx, PooledBlas& blas, size_t bufferSize, const char* name);
  void refreshBlasStats();
  void resetBlasLifecycle(PooledBlas& blas);
  PendingGpuTiming beginGpuTiming(Rc<DxvkContext> ctx, GpuTimingScope scope);
  void endGpuTiming(Rc<DxvkContext> ctx, PendingGpuTiming&& timing);
  void pollGpuTimings();
  void resetGpuTimingStats();

  std::vector<RtInstance*> m_reorderedSurfaces;
  std::vector<uint32_t> m_reorderedSurfacesFirstIndexOffset;
  std::vector<uint32_t> m_reorderedSurfacesPrimitiveIDPrefixSum;              // Exclusive prefix sum for this frame's surface primitive count array
  std::vector<uint32_t> m_reorderedSurfacesPrimitiveIDPrefixSumLastFrame;     // Exclusive prefix sum for last frame's surface primitive count array
  std::vector<VkAccelerationStructureInstanceKHR> m_mergedInstances[Tlas::Count];
  std::vector<Rc<PooledBlas>> m_blasPool;

  // GPU-driven PointInstancer culling batches, recorded per frame in mergeInstancesIntoBlas
  std::vector<PointInstancerBatch> m_pointInstancerBatches;

  // Number of VkAccelerationStructureInstanceKHR slots reserved for PointInstancer
  // instances in each TLAS type.  These slots are NOT stored in m_mergedInstances —
  // the GPU culling shader fills them directly in the instance buffer.
  uint32_t m_pointInstancerSlotsPerType[Tlas::Count] = {};

  // --- Incremental BLAS build caching ---
  // Scene generation from InstanceManager when the BLAS was last built.
  uint64_t m_lastProcessedGeneration = UINT64_MAX;
  bool m_sceneUnchangedThisFrame = false;
  // Set when newly built OMMs need to be bound to BLASes.  Forces all buckets
  // dirty on the next incremental rebuild so tryBindOpacityMicromap runs.
  bool m_ommBindPending = false;

  // Number of non-billboard entries in m_mergedInstances per TLAS type.
  // prepareSceneData() truncates to this baseline before appending fresh billboard instances.
  uint32_t m_mergedInstancesBaselineCount[Tlas::Count] = {};

  // Tracks all dynamic BLAS references from the last full build so they can be
  // touched during the cached (skip) path to prevent GC from collecting them.
  std::vector<Rc<PooledBlas>> m_activeDynamicBlases;
  std::vector<Rc<PooledBlas>> m_activeMergedBlases;

  // --- Dynamics-only rebuild cached state ---
  // Per-bucket cache from the last build.  Each entry corresponds to one merged
  // BLAS bucket and stores all the data needed to restore its portion of
  // m_reorderedSurfaces and m_mergedInstances without re-running the bucket pipeline.
  struct CachedBucketState {
    // The instances that contributed geometry to this bucket (for dirty checking)
    std::vector<RtInstance*> instances;
    std::vector<uint64_t> instanceCacheIdentities;
    // Surface data for m_reorderedSurfaces
    std::vector<RtInstance*> surfaces;
    std::vector<uint32_t> indexOffsets;
    // The PooledBlas assigned to this bucket (kept alive via Rc)
    Rc<PooledBlas> assignedBlas;
    // TLAS instance template (surface offset must be adjusted each frame)
    VkAccelerationStructureInstanceKHR tlasInstance {};
    // Which TLAS type(s) this bucket was emitted to
    bool isUnordered = false;
    bool hasSssInstances = false;
  };
  std::vector<CachedBucketState> m_cachedBuckets;

  // Maps a merged instance pointer to its bucket index in m_cachedBuckets.
  // Allows O(1) "is this instance in a clean bucket?" check in the main loop.
  std::unordered_map<RtInstance*, uint32_t> m_instanceBucketIndex;

  // Set of BlasEntry* that went to the dynamic path on the last full rebuild.
  // Used for quick O(1) per-instance classification on the dynamics-only path.
  std::unordered_set<BlasEntry*> m_cachedDynamicBlasEntries;

  // Scratch containers for collecting dynamic BLAS groups during mergeInstancesIntoBlas().
  // The map is lookup-only; the vector preserves first-seen emission order.
  std::vector<UniqueBlasInstances> m_uniqueDynamicBlas;
  uint32_t m_uniqueDynamicBlasCount = 0;
  std::unordered_map<BlasEntry*, uint32_t> m_uniqueDynamicBlasIndex;

  void resetUniqueDynamicBlasGroups();

  Rc<DxvkBuffer> m_vkInstanceBuffer; // Note: Holds Vulkan AS Instances, not RtInstances
  Rc<DxvkBuffer> m_surfaceBuffer;
  Rc<DxvkBuffer> m_surfaceMappingBuffer;
  Rc<DxvkBuffer> m_transformBuffer;
  Rc<DxvkBuffer> m_primitiveIDPrefixSumBuffer;
  Rc<DxvkBuffer> m_primitiveIDPrefixSumBufferLastFrame;

  int getCurrentFramePrimitiveIDPrefixSumBufferID() const;

  Rc<PooledBlas> m_intersectionBlas;
  Rc<DxvkBuffer> m_aabbBuffer;
  Rc<DxvkBuffer> m_billboardsBuffer;
  void createAndBuildIntersectionBlas(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers);
  
  Rc<DxvkBuffer> getScratchMemory(const size_t requiredScratchAllocSize);
  Rc<DxvkAccelStructure> createBlasAccelerationStructure(size_t bufferSize, const char* name) const;
  Rc<PooledBlas> createPooledBlas(size_t bufferSize, const char* name) const;

  // The recorder currently lives here because it only captures acceleration
  // structure state. Move it higher if crash recording expands beyond AS data.
  RtxGpuCrashRecorder m_gpuCrashRecorder;
  VkQueryPool m_compactionQueryPool = VK_NULL_HANDLE;
  std::array<CompactionQuerySlot, kCompactionQueryCount> m_compactionQuerySlots;
  uint32_t m_nextCompactionQuery = 0;
  uint32_t m_maintenanceOperationsThisFrame = 0;
  bool m_compactionQueryPoolCreationFailed = false;
  bool m_spatialClusteringConfigInitialized = false;
  bool m_previousSpatialClusteringEnabled = false;
  uint32_t m_previousSpatialClusterMaxPrimitives = 0;
  uint32_t m_previousSpatialClusterMaxGeometries = 0;
  BlasStats m_blasStats;
  BlasActivityStats m_blasActivityThisFrame;
  MergedBlasChurnStats m_mergedBlasChurnStats;
  GpuTimingStats m_gpuTimingStats;
  PendingGpuTiming m_activeFrameGpuTiming;
  PendingGpuTiming m_activeRayTracingGpuTiming;
  std::vector<PendingGpuTiming> m_pendingGpuTimings;
  uint64_t m_gpuTimingGeneration = 0;
  VkDeviceSize m_scratchAlignment;
  Rc<DxvkBuffer> m_scratchBuffer;
};

}  // namespace dxvk
