/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_include.h"
#include "rtx_resources.h"
#include "dxvk_image.h"

namespace dxvk {

  class DxvkCommandList;
  class DxvkBarrierSet;
  class DxvkContext;

  enum class FSR3Profile : uint32_t {
    UltraPerf = 0,
    MaxPerf,
    Balanced,
    MaxQuality,
    Auto,
    FullResolution,
    Invalid
  };

  const char* fsr3ProfileToString(FSR3Profile fsr3Profile);

  class DxvkFSR3 : public CommonDeviceObject, public RtxPass {
  public:
    enum class MotionVectorScale : uint32_t {
      Absolute,   ///< Motion vectors are provided in absolute screen space length (pixels).
      Relative,   ///< Motion vectors are provided in relative screen space length (pixels divided by screen width/height).
    };

    explicit DxvkFSR3(DxvkDevice* device);
    ~DxvkFSR3();

    bool supportsFSR3() const;

    void setSetting(const uint32_t displaySize[2], const FSR3Profile profile, uint32_t outRenderSize[2]);
    // Gets the profile FSR3 is currently using (the actual profile, not the settings-based one
    // which may be Auto for example).
    FSR3Profile getCurrentProfile() const;
    // Gets the input (the potentially lower resolution) size to be provided to FSR3.
    void getInputSize(uint32_t& width, uint32_t& height) const;
    // Gets the output (the potentially upscaled higher resolution) size to be provided to FSR3.
    void getOutputSize(uint32_t& width, uint32_t& height) const;

    void dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory = false);

    void showImguiSettings();

    void onDestroy();

    void release();

  protected:
    virtual bool isEnabled() const override;

    static FSR3Profile getAutoProfile(uint32_t displayWidth, uint32_t displayHeight);
    static uint32_t profileToQualityMode(FSR3Profile profile);

    void initializeFSR3(Rc<DxvkContext> pRenderContext);

    // Options
    FSR3Profile                 mProfile = FSR3Profile::Invalid;
    FSR3Profile                 mActualProfile = FSR3Profile::Invalid;
    MotionVectorScale           mMotionVectorScale = MotionVectorScale::Absolute;
    bool                        mIsHDR = true;
    float                       mPreExposure = 1.f;
    bool                        mInverseDepth = false;

    bool                        mRecreate = true;
    uint32_t                    mInputSize[2] = {};            ///< Input size in pixels.
    uint32_t                    mFSR3OutputSize[2] = {};       ///< FSR3 output size in pixels.

    // FSR3 specific settings
    float                       mSharpness = 0.5f;
    bool                        mReactiveMaskEnabled = false;
    
    // FSR3 context/resources would go here
    // (placeholder for actual FSR3 SDK integration)
  };
}  // namespace dxvk 