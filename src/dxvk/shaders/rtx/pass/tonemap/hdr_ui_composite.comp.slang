/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

// HDR UI Composite Shader
// Composites SDR UI content onto PQ-encoded HDR background
// Fixes blown-out UI elements when SDR UI is drawn onto PQ surfaces

#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx/utility/color.slangh"

layout(binding = HDR_UI_COMPOSITE_HDR_INPUT)
Texture2D<float4> HDRInput;

layout(binding = HDR_UI_COMPOSITE_UI_INPUT)
Texture2D<float4> UIInput;

layout(binding = HDR_UI_COMPOSITE_OUTPUT)
RWTexture2D<float4> Output;

layout(push_constant)
ConstantBuffer<HDRUICompositeArgs> cb;

// sRGB to linear conversion (inverse sRGB EOTF)
float3 sRGBToLinear(float3 srgb) 
{
  float3 result;
  for (int i = 0; i < 3; ++i) {
    if (srgb[i] <= 0.04045f) {
      result[i] = srgb[i] / 12.92f;
    } else {
      result[i] = pow((srgb[i] + 0.055f) / 1.055f, 2.4f);
    }
  }
  return result;
}

// Rec.709 to Rec.2020 color space conversion for HDR10
float3 rec709ToRec2020(float3 color) 
{
  const float3x3 conversionMatrix = float3x3(
    0.6274040f,  0.3292820f,  0.0433136f,
    0.0690970f,  0.9195400f,  0.0113612f,
    0.0163916f,  0.0880132f,  0.8955950f
  );
  return mul(conversionMatrix, color);
}

// Linear to PQ (ST.2084) transfer function
float3 linearToPQ(float3 color) 
{
  const float m1 = 0.1593017578125f;
  const float m2 = 78.84375f;
  const float c1 = 0.8359375f;
  const float c2 = 18.8515625f;
  const float c3 = 18.6875f;
  
  color = clamp(color, float3(0.0f), float3(1.0f));
  
  float3 result;
  for (int i = 0; i < 3; ++i) {
    if (color[i] <= 0.0f) {
      result[i] = 0.0f;
    } else {
      float powM1 = pow(color[i], m1);
      result[i] = pow((c1 + c2 * powM1) / (1.0f + c3 * powM1), m2);
    }
  }
  return result;
}


// Convert SDR UI color (sRGB) to PQ-encoded HDR
// This properly scales UI brightness relative to paper white
float3 convertUIToPQ(float3 uiColor) 
{
  // 1. Decode sRGB gamma to linear
  float3 linear = sRGBToLinear(uiColor);
  
  // 2. Convert to Rec.2020 gamut for HDR10 compatibility
  float3 rec2020 = rec709ToRec2020(linear);
  
  // 3. Scale to nits: SDR white (1.0) = paper white luminance
  //    Then normalize to PQ's 10,000 nit reference
  const float PQ_MAX_NITS = 10000.0f;
  float3 scaledNits = rec2020 * cb.paperWhiteNits;
  float3 normalized = scaledNits / PQ_MAX_NITS;
  
  // 4. Apply PQ transfer function
  return linearToPQ(normalized);
}

// Convert UI color to HDR10 (BT.2020 + PQ)
float3 convertUIToHDR(float3 uiColor)
{
  // Always use PQ/HDR10 format
  return convertUIToPQ(uiColor);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  // Sample the PQ-encoded HDR background (stashed from injectRTX)
  float4 hdrColor = HDRInput[threadId];
  
  // Sample the UI layer (rendered on cleared black backbuffer)
  float4 uiColor = UIInput[threadId];
  
  float4 result;
  
  // Full color mode - render UI exactly as-is with proper HDR10 color conversion
  // Only exact black (cleared background) is treated as transparent
  const float exactBlackThreshold = 0.004f; // ~1/255 to catch cleared background
  
  bool hasVisibleColor = (uiColor.r >= exactBlackThreshold || 
                          uiColor.g >= exactBlackThreshold || 
                          uiColor.b >= exactBlackThreshold);
  bool hasAlpha = (uiColor.a >= exactBlackThreshold);
  bool isBackground = !hasVisibleColor && !hasAlpha;
  
  if (isBackground) {
    // Cleared background - show HDR content
    result = hdrColor;
  } else if (hasVisibleColor && !hasAlpha) {
    // Opaque UI without alpha - common in Source Engine games
    float3 uiHDR = convertUIToHDR(uiColor.rgb);
    result = float4(uiHDR, 1.0f);
  } else if (hasAlpha) {
    // Semi-transparent UI with alpha
    float alpha = uiColor.a;
    
    if (alpha >= 1.0f - exactBlackThreshold) {
      // Fully opaque
      float3 uiHDR = convertUIToHDR(uiColor.rgb);
      result = float4(uiHDR, 1.0f);
    } else {
      // Semi-transparent - un-premultiply and blend
      float3 originalColor = uiColor.rgb / max(alpha, 0.01f);
      originalColor = clamp(originalColor, float3(0.0f), float3(1.0f));
      float3 uiHDR = convertUIToHDR(originalColor);
      result = float4(lerp(hdrColor.rgb, uiHDR, alpha), 1.0f);
    }
  } else {
    // Fallback - show HDR content
    result = hdrColor;
  }
  
  Output[threadId] = result;
}

