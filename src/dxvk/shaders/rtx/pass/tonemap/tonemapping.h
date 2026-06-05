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
#ifndef TONEMAPPING_H
#define TONEMAPPING_H

#include "rtx/utility/shader_types.h"

#define AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT              0
#define AUTO_EXPOSURE_EXPOSURE_INPUT_OUTPUT               1
#define AUTO_EXPOSURE_COLOR_INPUT                         2
#define AUTO_EXPOSURE_DEBUG_VIEW_OUTPUT                   3

#define TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT         0
#define TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT      1
#define TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT        2

#define EXPOSURE_HISTOGRAM_SIZE                           256

static const uint32_t tonemapOperatorNone           = 0;
static const uint32_t tonemapOperatorACESHill       = 1;
static const uint32_t tonemapOperatorACESNarkowicz  = 2;
static const uint32_t tonemapOperatorHableFilmic    = 3;
static const uint32_t tonemapOperatorAgX            = 4;
static const uint32_t tonemapOperatorLottes         = 5;
static const uint32_t tonemapOperatorPsycho17       = 6;
static const uint32_t tonemapOperatorGT7            = 7;
static const uint32_t tonemapOperatorNeutwo         = 8;

struct ToneMappingAutoExposureArgs {
  uint  numPixels;
  float lightAdaptTau;
  float darkAdaptTau;
  float deltaTime;

  uint  debugMode;
  uint  pad0;
  uint  pad1;
  uint  pad2;
};

struct ToneMappingApplyToneMappingArgs {
  uint toneMappingEnabled;
  uint enableAutoExposure;
  uint colorGradingEnabled;
  uint tonemapOperator;

  float exposureFactor;
  float contrast;
  float saturation;
  float pad0;

  vec3 colorBalance;
  uint pad1;

  float hableExposureBias;
  float hableShoulderStrength;
  float hableLinearStrength;
  float hableLinearAngle;

  float hableToeStrength;
  float hableToeNumerator;
  float hableToeDenominator;
  float hableWhitePoint;

  float agxSaturation;
  uint  agxLook;
  float agxPad0;
  float agxPad1;

  float psycho17PeakValue;
  float psycho17Exposure;
  float psycho17Highlights;
  float psycho17Shadows;

  float psycho17Contrast;
  float psycho17PurityScale;
  float psycho17BleachingIntensity;
  float psycho17ClipPoint;

  float psycho17HueRestore;
  float psycho17AdaptationContrast;
  uint  psycho17WhiteCurveMode;
  float psycho17ConeResponseExponent;

  float psycho17GamutCompression;
  uint  psycho17GamutCompressionMode;
  float psycho17Pad0;
  float psycho17Pad1;
};

#ifdef __cplusplus
static_assert(sizeof(ToneMappingApplyToneMappingArgs) == 160,
              "ToneMappingApplyToneMappingArgs layout drift.");
#endif

#endif
