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

// Linear to HLG (Hybrid Log-Gamma) transfer function
float3 linearToHLG(float3 color) 
{
  const float a = 0.17883277f;
  const float b = 0.28466892f;
  const float c = 0.55991073f;
  const float gamma = 1.2f;
  const float threshold = 1.0f / 12.0f;
  
  color = clamp(color, float3(0.0f), float3(1.0f));
  
  float3 result;
  for (int i = 0; i < 3; ++i) {
    if (color[i] <= 0.0f) {
      result[i] = 0.0f;
    } else if (color[i] <= threshold) {
      result[i] = sqrt(3.0f * color[i]);
    } else {
      result[i] = a * log(12.0f * color[i] - b) + c;
    }
  }
  
  return pow(max(result, float3(0.0f)), 1.0f / gamma);
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

// Convert SDR UI color (sRGB) to HLG-encoded HDR
float3 convertUIToHLG(float3 uiColor)
{
  // 1. Decode sRGB gamma to linear
  float3 linear = sRGBToLinear(uiColor);
  
  // 2. Convert to Rec.2020 gamut
  float3 rec2020 = rec709ToRec2020(linear);
  
  // 3. Scale for HLG (normalize to display peak)
  float3 scaledNits = rec2020 * cb.paperWhiteNits;
  float3 normalized = scaledNits / max(cb.maxLuminanceNits, 1.0f);
  
  // 4. Apply HLG transfer function
  return linearToHLG(normalized);
}

// Blend mode constants (must match HDRUIBlendMode enum in rtx_tone_mapping.h)
static const uint BLEND_MODE_ALPHA = 0;           // Standard alpha blend
static const uint BLEND_MODE_LUMINANCE = 1;       // Luminance-based detection (original)
static const uint BLEND_MODE_ALPHA_REPLACE = 2;   // Hard replacement where alpha > 0
static const uint BLEND_MODE_ADDITIVE = 3;        // Additive blend (preserves black naturally)
static const uint BLEND_MODE_PRESERVE_BLACK = 4;  // Alpha blend but treat black as transparent
static const uint BLEND_MODE_FULL_COLOR = 5;      // Render UI as-is, only exact black is transparent

// Convert UI color to HDR based on current format setting
float3 convertUIToHDR(float3 uiColor)
{
  if (cb.hdrFormat == 1) {
    // PQ/HDR10 format
    return convertUIToPQ(uiColor);
  } else if (cb.hdrFormat == 2) {
    // HLG format - uses configured max luminance
    return convertUIToHLG(uiColor);
  } else {
    // Linear format - just decode sRGB and scale by paper white
    float3 linear = sRGBToLinear(uiColor);
    return linear * (cb.paperWhiteNits / 100.0f); // Normalize relative to 100 nits SDR reference
  }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  // Sample the PQ-encoded HDR background (stashed from injectRTX)
  float4 hdrColor = HDRInput[threadId];
  
  // Sample the SDR UI layer (sRGB, drawn on cleared backbuffer)
  float4 uiColor = UIInput[threadId];
  
  float4 result;
  
  // Compute luminance for detection modes
  float uiLuminance = calcBt709Luminance(uiColor.rgb);
  
  // Determine effective alpha - many games (like Source Engine) render UI without proper alpha,
  // so we need to detect UI presence via luminance when alpha is 0
  bool hasUIContent = uiLuminance > cb.uiDetectionThreshold;
  float baseAlpha = uiColor.a;
  
  // If alpha is 0 but we have visible UI content, the game isn't using alpha properly
  // In this case, treat it as fully opaque UI
  if (baseAlpha <= 0.0f && hasUIContent) {
    baseAlpha = 1.0f;
  }
  
  // Select blending strategy based on mode
  switch (cb.blendMode) {
    case BLEND_MODE_ALPHA: {
      // Standard alpha blend - use UI alpha channel directly
      // Black pixels with alpha=1 WILL darken the background
      if (baseAlpha > 0.0f) {
        float3 uiHDR = convertUIToHDR(uiColor.rgb);
        result = float4(lerp(hdrColor.rgb, uiHDR, baseAlpha), 1.0f);
      } else {
        result = hdrColor;
      }
      break;
    }
    
    case BLEND_MODE_LUMINANCE: {
      // Luminance-based detection (original behavior)
      // Only blend where UI luminance exceeds threshold
      // Preserves black pixels but may miss very dark UI elements
      if (hasUIContent) {
        float3 uiHDR = convertUIToHDR(uiColor.rgb);
        result = float4(lerp(hdrColor.rgb, uiHDR, baseAlpha), 1.0f);
      } else {
        result = hdrColor;
      }
      break;
    }
    
    case BLEND_MODE_ALPHA_REPLACE: {
      // Hard replacement where alpha > 0 or luminance detected (no blending)
      // Good for crisp UI elements
      if (baseAlpha > 0.0f) {
        float3 uiHDR = convertUIToHDR(uiColor.rgb);
        result = float4(uiHDR, 1.0f);
      } else {
        result = hdrColor;
      }
      break;
    }
    
    case BLEND_MODE_ADDITIVE: {
      // Additive blend - naturally preserves black (adding 0 = no change)
      // Great for glowing UI elements, may over-brighten white UI on bright scenes
      float3 uiHDR = convertUIToHDR(uiColor.rgb);
      // Scale additive contribution by alpha if available
      result = float4(hdrColor.rgb + uiHDR * baseAlpha, 1.0f);
      break;
    }
    
    case BLEND_MODE_PRESERVE_BLACK: {
      // Alpha blend but treat black/near-black as transparent
      // Uses luminance to compute an "effective alpha" that fades black to transparent
      // This preserves black pixels while still allowing alpha blending for non-black UI
      
      // Compute luminance-based opacity: black (lum=0) = transparent, white (lum=1) = opaque
      // Use threshold to control the transition
      float lumAlpha = saturate(uiLuminance / max(cb.uiDetectionThreshold * 10.0f, 0.1f));
      
      // Combine with base alpha (which may have been set to 1.0 if game doesn't use alpha)
      float effectiveAlpha = lumAlpha * baseAlpha;
      
      if (effectiveAlpha > 0.001f) {
        float3 uiHDR = convertUIToHDR(uiColor.rgb);
        result = float4(lerp(hdrColor.rgb, uiHDR, effectiveAlpha), 1.0f);
      } else {
        result = hdrColor;
      }
      break;
    }
    
    case BLEND_MODE_FULL_COLOR:
    default: {
      // Full color mode - render UI exactly as-is with color space conversion
      // Supports semi-transparent UI elements via proper alpha compositing
      
      const float exactBlackThreshold = 0.004f; // ~1/255
      
      // Check if this pixel has any UI content
      bool hasVisibleColor = (uiColor.r >= exactBlackThreshold || 
                              uiColor.g >= exactBlackThreshold || 
                              uiColor.b >= exactBlackThreshold);
      bool hasAlpha = (uiColor.a >= exactBlackThreshold);
      
      // Cleared background = no color AND no alpha
      bool isBackground = !hasVisibleColor && !hasAlpha;
      
      if (isBackground) {
        // This is cleared background - show HDR through
        result = hdrColor;
      } else if (hasVisibleColor && !hasAlpha) {
        // Game doesn't use alpha channel - treat UI as fully opaque
        float3 uiHDR = convertUIToHDR(uiColor.rgb);
        result = float4(uiHDR, 1.0f);
      } else if (hasAlpha) {
        // Game uses alpha - handle semi-transparent UI properly
        // The UI was rendered onto black, so the color is pre-multiplied with alpha
        // (e.g., white at 50% opacity became gray after blending with black)
        
        float alpha = uiColor.a;
        
        if (alpha >= 1.0f - exactBlackThreshold) {
          // Fully opaque - direct replacement
          float3 uiHDR = convertUIToHDR(uiColor.rgb);
          result = float4(uiHDR, 1.0f);
        } else {
          // Semi-transparent - use pre-multiplied alpha compositing
          // The UI color is already "pre-multiplied" (blended with black)
          // So we need to un-premultiply first, then convert, then blend
          
          // Un-premultiply: recover original color before it was blended with black
          // color_original = color_blended / alpha (clamped to avoid division issues)
          float3 originalColor = uiColor.rgb / max(alpha, 0.01f);
          originalColor = clamp(originalColor, float3(0.0f), float3(1.0f));
          
          // Convert the original color to HDR
          float3 uiHDR = convertUIToHDR(originalColor);
          
          // Standard alpha blend with HDR background
          result = float4(lerp(hdrColor.rgb, uiHDR, alpha), 1.0f);
        }
      } else {
        // Edge case: has alpha but no color (shouldn't happen often)
        result = hdrColor;
      }
      break;
    }
  }
  
  Output[threadId] = result;
}

