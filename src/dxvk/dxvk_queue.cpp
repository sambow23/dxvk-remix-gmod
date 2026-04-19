/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "dxvk_device.h"
#include "dxvk_queue.h"
#include "dxvk_scoped_annotation.h"

#include "../util/util_string.h"

#include "NvLowLatencyVk.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace dxvk {
  namespace {
    constexpr uint64_t kPresentPerfLogIntervalFrames = 60;

    uint64_t currentTimestampNanoseconds() {
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    uint64_t toNanoseconds(std::chrono::steady_clock::duration duration) {
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    }

    void accumulatePresentPerf(uint64_t duration, uint64_t& total, uint64_t& max) {
      total += duration;
      max = std::max(max, duration);
    }

    std::string formatMilliseconds(uint64_t nanoseconds, uint64_t divisor) {
      std::ostringstream stream;
      const double milliseconds = divisor == 0
        ? 0.0
        : static_cast<double>(nanoseconds) / static_cast<double>(divisor) / 1000000.0;
      stream << std::fixed << std::setprecision(3) << milliseconds;
      return stream.str();
    }

    std::string formatMillisecondsMax(uint64_t nanoseconds) {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(3)
             << static_cast<double>(nanoseconds) / 1000000.0;
      return stream.str();
    }
  }

  
  DxvkSubmissionQueue::DxvkSubmissionQueue(DxvkDevice* device)
  : m_device(device),
    m_submitThread([this] () { submitCmdLists(); }),
    m_finishThread([this] () { finishCmdLists(); }) {
  }
  
  
  DxvkSubmissionQueue::~DxvkSubmissionQueue() {
    { std::unique_lock<dxvk::mutex> lock(m_mutex);
      m_stopped.store(true);
    }
    
    m_appendCond.notify_all();
    m_submitCond.notify_all();

    m_submitThread.join();
    m_finishThread.join();
  }
  
  
  void DxvkSubmissionQueue::submit(DxvkSubmitInfo submitInfo) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_finishCond.wait(lock, [this] {
      return m_submitQueue.size() + m_finishQueue.size() <= MaxNumQueuedCommandBuffers;
    });

    DxvkSubmitEntry entry = { };
    entry.submit = std::move(submitInfo);

    m_pending += 1;
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }


  void DxvkSubmissionQueue::present(DxvkPresentInfo presentInfo, DxvkSubmitStatus* status) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    if (status != nullptr) {
      status->queueEnterNanoseconds.store(currentTimestampNanoseconds());
      status->queueStartNanoseconds.store(0ull);
      status->queueEndNanoseconds.store(0ull);
      status->queueDepthAtEnqueue.store(static_cast<uint32_t>(m_submitQueue.size()));
      status->pendingSubmissionsAtEnqueue.store(m_pending.load());
      status->submittedCommandListsAtEnqueue.store(m_submittedCommandLists.load());
      status->completedCommandListsAtEnqueue.store(m_completedCommandLists.load());
    }

    DxvkSubmitEntry entry = { };
    entry.status  = status;
    entry.present = std::move(presentInfo);
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }


// NV-DXVK begin: DLFG integration
  void DxvkSubmissionQueue::setupFrameInterpolation(DxvkFrameInterpolationInfo frameInterpolationInfo) {
    ZoneScoped;
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    DxvkSubmitEntry entry = { };
    entry.frameInterpolation = std::move(frameInterpolationInfo);
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }
// NV-DXVK end

  void DxvkSubmissionQueue::synchronizeSubmission(
          DxvkSubmitStatus*   status) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_submitCond.wait(lock, [status] {
      return status->result.load() != VK_NOT_READY;
    });
  }


  void DxvkSubmissionQueue::synchronize() {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_submitCond.wait(lock, [this] {
      return m_submitQueue.empty();
    });

    // NV-DXVK start: DLFG integration
    if (m_lastPresenter != nullptr) {
      m_lastPresenter->synchronize();
      m_lastPresenter = nullptr;
    }
    // NV-DXVK end
  }


  void DxvkSubmissionQueue::lockDeviceQueue() {
    ScopedCpuProfileZone();
    m_mutexQueue.lock();
  }


  void DxvkSubmissionQueue::unlockDeviceQueue() {
    ScopedCpuProfileZone();
    m_mutexQueue.unlock();
  }

  void DxvkSubmissionQueue::submitCmdLists() {
    env::setThreadName("dxvk-submit");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (!m_stopped.load()) {
      m_appendCond.wait(lock, [this] {
        return m_stopped.load() || !m_submitQueue.empty();
      });
      
      if (m_stopped.load())
        return;

      ScopedCpuProfileZone();

      DxvkSubmitEntry entry = std::move(m_submitQueue.front());
      lock.unlock();
      
      // Submit command buffer to device
      VkResult status = VK_NOT_READY;

      if (m_lastError != VK_ERROR_DEVICE_LOST) {
        // NV-DXVK start: Rename lock to lockQueue to avoid shadowing other mutex
        std::lock_guard<dxvk::mutex> lockQueue(m_mutexQueue);
        // NV-DXVK end

          // NV-DXVK start: Reflex render submit
        const auto& reflex = m_device->getCommon()->metaReflex();
        // NV-DXVK end

        if (entry.submit.cmdList != nullptr) {
          // When using Reflex with Remix, we need to wrap the queue submit for the injectRTX rendering
          // work with the reflex render_submit markers.  This is because in Remix we essentially
          // have one large cmd list of work (inject rtx) and we want the Reflex timing to prioritize
          // this work for best latency reduction while minimizing performance impact.  So we tag the submit
          // upstream (RtxContext) which contains the injectRTX call as the one we want to wrap with Reflex markers.
          // NV-DXVK start: Reflex render submit
          if (entry.submit.insertReflexRenderMarkers) {
            reflex.beginRendering(entry.submit.cachedReflexFrameId);
          }

          status = entry.submit.cmdList->submit(
            entry.submit.waitSync,
            entry.submit.wakeSync);

          if (status == VK_SUCCESS)
            m_submittedCommandLists += 1;

          if (entry.submit.insertReflexRenderMarkers) {
            reflex.endRendering(entry.submit.cachedReflexFrameId);
          }
          // NV-DXVK end
        }
        // NV-DXVK start: DLFG integration
        else if (entry.frameInterpolation.valid()) {
          // stash frame interpolation data for next present call
          m_currentFrameInterpolationData = entry.frameInterpolation;
        }
        else if (entry.present.presenter != nullptr) {
          const auto presentStart = std::chrono::steady_clock::now();
          const uint64_t presentStartTimestamp = currentTimestampNanoseconds();

          if (entry.status != nullptr)
            entry.status->queueStartNanoseconds.store(presentStartTimestamp);

          m_lastPresenter = entry.present.presenter;

          // NV-DXVK start: Reflex present start
          const auto insertReflexPresentMarkers = entry.present.insertReflexPresentMarkers;
          const auto cachedReflexFrameId = entry.present.cachedReflexFrameId;

          // Note: Only insert Reflex Present markers around the Presenter's present call if requested.
          if (insertReflexPresentMarkers) {
            reflex.beginPresentation(cachedReflexFrameId);
          }
          // NV-DXVK end

          // NV-DXVK start: DLFG acquired image information retrieval
          const auto cachedAcquiredImageIndex = entry.present.cachedAcquiredImageIndex;
          // NV-DXVK end

          // m_device->vkd()->vkQueueWaitIdle(m_device->queues().graphics.queueHandle);
          const auto presentCallStart = std::chrono::steady_clock::now();
          status = entry.present.presenter->presentImage(&entry.status->result, entry.present, m_currentFrameInterpolationData, cachedAcquiredImageIndex);
          const auto presentCallNanoseconds = toNanoseconds(std::chrono::steady_clock::now() - presentCallStart);
          // if both submit and DLFG+present run on the same queue, then we need to wait for present to avoid racing on the queue
#if __DLFG_USE_GRAPHICS_QUEUE
          entry.present.presenter->synchronize();
#endif

          // NV-DXVK start: Reflex present end
          // Note: Only insert Reflex Present markers around the Presenter's present call if requested.
          if (insertReflexPresentMarkers) {
            reflex.endPresentation(cachedReflexFrameId);
          }
          // NV-DXVK end

          m_currentFrameInterpolationData.reset();

          const auto presentThrottleDelay = m_device->config().presentThrottleDelay;
          uint64_t throttleSleepNanoseconds = 0;

          if (presentThrottleDelay > 0) {
            ScopedCpuProfileZoneN("Present Throttle Delay Sleep");

            const auto throttleSleepStart = std::chrono::steady_clock::now();
            Sleep(presentThrottleDelay);
            throttleSleepNanoseconds = toNanoseconds(std::chrono::steady_clock::now() - throttleSleepStart);
          }

          if (entry.status != nullptr)
            entry.status->queueEndNanoseconds.store(currentTimestampNanoseconds());

          const uint64_t totalNanoseconds = toNanoseconds(std::chrono::steady_clock::now() - presentStart);
          auto& perf = m_presentPerfStats;
          perf.frames += 1;
          accumulatePresentPerf(totalNanoseconds, perf.totalNanoseconds, perf.totalMaxNanoseconds);
          accumulatePresentPerf(presentCallNanoseconds, perf.presentCallNanoseconds, perf.presentCallMaxNanoseconds);
          accumulatePresentPerf(throttleSleepNanoseconds, perf.throttleSleepNanoseconds, perf.throttleSleepMaxNanoseconds);

          if (perf.frames >= kPresentPerfLogIntervalFrames) {
            Logger::info(str::format(
              "DXVK queue present frames=", perf.frames,
              " totalAvgMs=", formatMilliseconds(perf.totalNanoseconds, perf.frames),
              " totalMaxMs=", formatMillisecondsMax(perf.totalMaxNanoseconds),
              " presentCallAvgMs=", formatMilliseconds(perf.presentCallNanoseconds, perf.frames),
              " presentCallMaxMs=", formatMillisecondsMax(perf.presentCallMaxNanoseconds),
              " throttleSleepAvgMs=", formatMilliseconds(perf.throttleSleepNanoseconds, perf.frames),
              " throttleSleepMaxMs=", formatMillisecondsMax(perf.throttleSleepMaxNanoseconds)));
            perf = {};
          }
        }
      } else {
        // Don't submit anything after device loss
        // so that drivers get a chance to recover
        status = VK_ERROR_DEVICE_LOST;
      }

      if (entry.status)
        // NV-DXVK start: DLFG integration
        // if we queued for interpolation, then don't touch the output status here; DLFG presenter thread will update it (and may have already done so)
        if (status != VK_EVENT_SET) {
          if (entry.status->queueEndNanoseconds.load() == 0ull)
            entry.status->queueEndNanoseconds.store(currentTimestampNanoseconds());
          entry.status->result = status;
        }
        // NV-DXVK end

      // On success, pass it on to the queue thread
      lock = std::unique_lock<dxvk::mutex>(m_mutex);

      if (status == VK_SUCCESS) {
        if (entry.submit.cmdList != nullptr)
          m_finishQueue.push(std::move(entry));
      } else if (status == VK_ERROR_DEVICE_LOST || entry.submit.cmdList != nullptr) {
        Logger::err(str::format("DxvkSubmissionQueue: Command submission failed: ", status));
        m_lastError = status;
        
        if (m_device->config().enableAftermath) {
          // Stall the pending exception until aftermath has finished writing (or hits some error)
          uint32_t counter = 0;
          GFSDK_Aftermath_CrashDump_Status aftermathStatus = GFSDK_Aftermath_CrashDump_Status_NotStarted; 
          
          static const uint32_t kTimeoutPreventionLimit = 5000;
          
          while (counter < kTimeoutPreventionLimit) {
            GFSDK_Aftermath_GetCrashDumpStatus(&aftermathStatus);

            if (aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Finished || aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Unknown)
              break; // Our dump was written

            static const uint32_t kTimeoutPerTry = 100;
            Sleep(kTimeoutPerTry);
            counter += kTimeoutPerTry;
          }
        }
        m_device->waitForIdle();
      }

      m_submitQueue.pop();
      m_submitCond.notify_all();
    }
  }
  
  
  void DxvkSubmissionQueue::finishCmdLists() {
    env::setThreadName("dxvk-queue");

    while (!m_stopped.load()) {
      std::unique_lock<dxvk::mutex> lock(m_mutex);

      if (m_finishQueue.empty()) {
        auto t0 = dxvk::high_resolution_clock::now();

        m_submitCond.wait(lock, [this] {
          return m_stopped.load() || !m_finishQueue.empty();
        });

        auto t1 = dxvk::high_resolution_clock::now();
        m_gpuIdle += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      }

      if (m_stopped.load())
        return;

      ScopedCpuProfileZone();
      
      DxvkSubmitEntry entry = std::move(m_finishQueue.front());
      lock.unlock();
      
      VkResult status = m_lastError.load();
      
      if (status != VK_ERROR_DEVICE_LOST)
        status = entry.submit.cmdList->synchronize();
      
      if (status != VK_SUCCESS) {
        Logger::err(str::format("DxvkSubmissionQueue: Failed to sync fence: ", status));
        m_lastError = status;
        m_device->waitForIdle();
      }

      // Release resources and signal events, then immediately wake
      // up any thread that's currently waiting on a resource in
      // order to reduce delays as much as possible.
      entry.submit.cmdList->notifyObjects();
      m_completedCommandLists += 1;

      lock.lock();
      m_pending -= 1;

      m_finishQueue.pop();
      m_finishCond.notify_all();
      lock.unlock();

      // Free the command list and associated objects now
      entry.submit.cmdList->reset();
      m_device->recycleCommandList(entry.submit.cmdList);
    }
  }
}
