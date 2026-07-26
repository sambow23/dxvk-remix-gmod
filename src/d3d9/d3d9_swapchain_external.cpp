/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "d3d9_swapchain.h"
#include "d3d9_surface.h"
#include "d3d9_monitor.h"

#include "d3d9_hud.h"
#include "../util/util_env.h"
#include "../dxvk/rtx_render/rtx_bridge_message_channel.h"
#include "../dxvk/dxvk_scoped_annotation.h"

namespace dxvk {

  D3D9SwapchainExternal::D3D9SwapchainExternal(
        D3D9DeviceEx* pDevice,
        D3DPRESENT_PARAMETERS* pPresentParams,
  const D3DDISPLAYMODEEX* pFullscreenDisplayMode)
    : D3D9SwapChainEx(
        pDevice, pPresentParams, pFullscreenDisplayMode, false) {
    constexpr auto kD3D12Fence =
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    m_frameEndSemaphore = RtxSemaphore::createTimeline(
      pDevice->GetDXVKDevice().ptr(), "ExternalPresenter::frameEnd", 0,
      true, kD3D12Fence);
    m_frameResumeSemaphore = RtxSemaphore::createTimeline(
      pDevice->GetDXVKDevice().ptr(), "ExternalPresenter::frameResume", 0,
      true, kD3D12Fence);
    CreateBackBuffers(2);
  }

  D3D9SwapchainExternal::~D3D9SwapchainExternal() {
    ReleaseAllExternalFrames();
  }

  void D3D9SwapchainExternal::CreateBackBuffers(uint32_t) {
    ReleaseAllExternalFrames();
    m_externalImageHandles = {
      INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
    m_externalImages = {};
    m_externalGeneration = 0;
    m_externalFrameValue = 0;
    m_presentParams.BackBufferCount = 2;
    D3D9SwapChainEx::CreateBackBuffers(2);
    for (uint32_t i = 0; i < 2; ++i) {
      m_externalImages[i] =
        m_backBuffers[i]->GetCommonTexture()->GetImage();
    }
  }

  HRESULT STDMETHODCALLTYPE D3D9SwapchainExternal::Present(const RECT*, const RECT*, HWND, const RGNDATA*,  DWORD) {
    auto targetImage = m_backBuffers[0]->GetCommonTexture()->GetImage();

    auto& imageInfo = targetImage->info();

    m_parent->m_rtx.EndFrame(targetImage);

    m_parent->Flush();
    m_parent->SynchronizeCsThread();

    m_context->beginRecording(m_device->createCommandList());

    // Retrieve the image and image view to present
    auto swapImage = m_backBuffers[0]->GetCommonTexture()->GetImage();
    auto swapImageView = m_backBuffers[0]->GetImageView(false);

    VkSurfaceFormatKHR fmt;
    fmt.format = imageInfo.format;
    fmt.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    if (m_hud != nullptr)
      m_hud->render(m_context, fmt, { imageInfo.extent.width,imageInfo.extent.height });

    // TODO: Figure out if we want HUD rendering, and how to use it
    //m_device->getCommon()->getImgui().render(m_window, m_context, fmt, { imageInfo.extent.width,imageInfo.extent.height }, m_vsync);

    m_parent->m_rtx.OnPresent(targetImage);

    m_context->changeImageLayout(swapImage, VK_IMAGE_LAYOUT_GENERAL);
    m_context->emitMemoryBarrier(VK_DEPENDENCY_DEVICE_GROUP_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_ACCESS_SHADER_READ_BIT);

    const bool hasExternalConsumer = m_externalGeneration != 0;
    uint64_t frameValue = 0;
    if (hasExternalConsumer) {
      // Signal the D3D12 consumer after the imported image is complete.
      frameValue = ++m_externalFrameValue;
      m_context->getCommandList()->addSignalSemaphore(
        m_frameEndSemaphore->handle(), frameValue);
    }
    m_device->submitCommandList(m_context->endRecording(), VK_NULL_HANDLE, VK_NULL_HANDLE);


    m_parent->GetDXVKDevice()->incrementPresentCount();

    if (!hasExternalConsumer)
      return S_OK;

    // Rotate to the alternate exported image. Frame N+1 may render while the
    // consumer samples frame N; before the producer returns to frame N-1's
    // slot, wait for that older frame to be released. This preserves one frame
    // of producer/consumer overlap without ever overwriting a sampled image.
    for (uint32_t i = 1; i < m_backBuffers.size(); ++i) {
      m_backBuffers[i]->Swap(m_backBuffers[i - 1].ptr());
    }
    m_parent->m_flags.set(D3D9DeviceFlag::DirtyFramebuffer);

    if (frameValue > 1) {
      m_parent->EmitCs([this, releaseValue = frameValue - 1](DxvkContext* ctx) {
        ctx->getCommandList()->addWaitSemaphore(
          m_frameResumeSemaphore->handle(), releaseValue);
        ctx->flushCommandList();
      });
    }

    return S_OK;
  }

  bool D3D9SwapchainExternal::SetExternalD3D12Resources(
      const HANDLE (&imageHandles)[2],
      uint32_t width,
      uint32_t height,
      VkFormat format) {
    if (imageHandles[0] == nullptr ||
        imageHandles[0] == INVALID_HANDLE_VALUE ||
        imageHandles[1] == nullptr ||
        imageHandles[1] == INVALID_HANDLE_VALUE ||
        width == 0 || height == 0) {
      return false;
    }

    D3D9Format d3d9Format = D3D9Format::Unknown;
    switch (format) {
      case VK_FORMAT_R8G8B8A8_UNORM:
        d3d9Format = D3D9Format::A8B8G8R8;
        break;
      case VK_FORMAT_B8G8R8A8_UNORM:
        d3d9Format = D3D9Format::A8R8G8B8;
        break;
      default:
        return false;
    }

    WaitForAllExternalFrames();
    D3D9DeviceLock lock = m_parent->LockDevice();
    m_parent->Flush();
    m_parent->SynchronizeCsThread();
    m_device->waitForIdle();

    D3D9_COMMON_TEXTURE_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Depth = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = d3d9Format;
    desc.MultiSample = D3DMULTISAMPLE_NONE;
    desc.MultisampleQuality = 0;
    desc.Pool = D3DPOOL_DEFAULT;
    desc.Usage = D3DUSAGE_RENDERTARGET;
    desc.Discard = FALSE;
    desc.IsBackBuffer = TRUE;
    desc.IsAttachmentOnly = FALSE;

    std::array<Com<D3D9Surface, false>, 2> importedBuffers;
    std::array<Rc<DxvkImage>, 2> importedImages;
    try {
      for (uint32_t i = 0; i < 2; ++i) {
        HANDLE importHandle = imageHandles[i];
        importedBuffers[i] = new D3D9Surface(
          m_parent, &desc, this, &importHandle,
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT);
        importedImages[i] =
          importedBuffers[i]->GetCommonTexture()->GetImage();
      }
    } catch (const DxvkError& error) {
      Logger::err(str::format(
        "External presenter D3D12 resource import failed: ",
        error.message()));
      return false;
    }

    DestroyBackBuffers();
    m_presentParams.BackBufferWidth = width;
    m_presentParams.BackBufferHeight = height;
    m_presentParams.BackBufferCount = 2;
    m_presentParams.BackBufferFormat =
      static_cast<D3DFORMAT>(d3d9Format);
    m_presentParams.MultiSampleType = D3DMULTISAMPLE_NONE;
    m_presentParams.MultiSampleQuality = 0;
    m_backBuffers.assign(importedBuffers.begin(), importedBuffers.end());
    m_externalImageHandles = { imageHandles[0], imageHandles[1] };
    m_externalImages = importedImages;

    VkImageSubresourceRange subresources = {};
    subresources.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresources.levelCount = 1;
    subresources.layerCount = 1;
    VkClearColorValue clearColor = {};
    m_context->beginRecording(m_device->createCommandList());
    for (const auto& image : m_externalImages) {
      m_context->clearColorImage(image, clearColor, subresources);
    }
    m_device->submitCommandList(
      m_context->endRecording(), VK_NULL_HANDLE, VK_NULL_HANDLE);

    m_externalGeneration = ++m_externalGenerationSerial;
    m_externalFrameValue = 0;
    m_parent->m_flags.set(D3D9DeviceFlag::DirtyFramebuffer);
    return true;
  }

  bool D3D9SwapchainExternal::GetExternalD3D12FrameInfo(
      HANDLE (&imageHandles)[2],
      HANDLE& readyFenceHandle,
      HANDLE& releaseFenceHandle,
      uint64_t& generation,
      uint64_t& frameValue,
      uint32_t& width,
      uint32_t& height,
      VkFormat& format) const {
    D3D9DeviceLock lock = m_parent->LockDevice();
    if (m_externalGeneration == 0 ||
        m_externalImageHandles[0] == INVALID_HANDLE_VALUE ||
        m_externalImageHandles[1] == INVALID_HANDLE_VALUE ||
        m_frameEndSemaphore == nullptr || m_frameResumeSemaphore == nullptr) {
      return false;
    }
    imageHandles[0] = m_externalImageHandles[0];
    imageHandles[1] = m_externalImageHandles[1];
    readyFenceHandle = m_frameEndSemaphore->sharedHandle();
    releaseFenceHandle = m_frameResumeSemaphore->sharedHandle();
    generation = m_externalGeneration;
    frameValue = m_externalFrameValue;
    width = m_presentParams.BackBufferWidth;
    height = m_presentParams.BackBufferHeight;
    format = m_externalImages[0]->info().format;
    return true;
  }

  void D3D9SwapchainExternal::ReleaseAllExternalFrames() {
    if (m_frameResumeSemaphore != nullptr && m_externalFrameValue != 0) {
      const uint64_t current = m_frameResumeSemaphore->value();
      if (current < m_externalFrameValue) {
        m_frameResumeSemaphore->signal(m_externalFrameValue);
      }
    }
  }

  void D3D9SwapchainExternal::WaitForAllExternalFrames() {
    if (m_frameResumeSemaphore != nullptr && m_externalFrameValue != 0) {
      m_frameResumeSemaphore->wait(m_externalFrameValue);
    }
  }

}
