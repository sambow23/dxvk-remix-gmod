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
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping_noise.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/noise.slangh"

// HDR Processing binding indices are now defined in tonemapping.h

layout(binding = HDR_PROCESSING_BLUE_NOISE_TEXTURE)
Texture2DArray BlueNoise;

layout(binding = HDR_PROCESSING_INPUT_BUFFER)
RWTexture2D<float4> InColorBuffer;

layout(binding = HDR_PROCESSING_OUTPUT_BUFFER)
RWTexture2D<float4> OutColorBuffer;

layout(binding = HDR_PROCESSING_EXPOSURE_INPUT)
RWTexture1D<float> InExposure;

// HDR Processing Arguments (HDR10/PQ only)
struct HDRProcessingArgs {
  uint enableAutoExposure;
  float hdrMaxLuminance;
  float hdrMinLuminance;
  float hdrPaperWhiteLuminance;
  float exposureFactor;
  uint frameIndex;
  uint pad_hdrFormat;  // Reserved (HDR10/PQ is always used)
  float hdrExposureBias;
  float hdrBrightness;
  uint hdrToneMapper;  // 0=None, 1=ACES_HDR, 2=Frostbite
  uint hdrEnableDithering;
  float hdrShadows;
  float hdrMidtones;
  float hdrHighlights;
  float hdrBlueNoiseAmplitude;
  // Color grading
  float hdrColorBalanceR;
  float hdrColorBalanceG;
  float hdrColorBalanceB;
  float hdrSaturation;
  float hdrContrast;
  // Frostbite parameters
  float frostbiteRolloffStart;
  float frostbiteSaturation;
  float frostbiteHueCorrect;
  // Debug flags
  uint hdrShowGamutWarning;
  uint hdrShowLuminanceHeatmap;
  uint pad0;
  uint pad1;
};

layout(push_constant)
ConstantBuffer<HDRProcessingArgs> cb;

// Convert from Rec.709 to Rec.2020 color space for HDR
float3 rec709ToRec2020(float3 color) 
{
  // Conversion matrix from Rec.709 to Rec.2020
  // These are the official ITU-R BT.2020 conversion matrices
  const float3x3 conversionMatrix = float3x3(
    0.6274040f,  0.3292820f,  0.0433136f,
    0.0690970f,  0.9195400f,  0.0113612f,
    0.0163916f,  0.0880132f,  0.8955950f
  );
  
  return mul(conversionMatrix, color);
}

// Convert from linear RGB to PQ (Perceptual Quantizer) for HDR10
float3 linearToPQ(float3 color) 
{
  // PQ constants for HDR10 (ST.2084)
  const float m1 = 0.1593017578125f; // 2610.0 / 16384.0
  const float m2 = 78.84375f; // 2523.0 / 32.0
  const float c1 = 0.8359375f; // 3424.0 / 4096.0
  const float c2 = 18.8515625f; // 2413.0 / 128.0
  const float c3 = 18.6875f; // 2392.0 / 128.0
  
  // Clamp input to valid range [0, 1] for PQ
  color = clamp(color, float3(0.0f), float3(1.0f));
  
  // Apply PQ curve per channel with safety checks
  float3 result;
  for (int i = 0; i < 3; ++i) {
    float channel = color[i];
    if (channel <= 0.0f) {
      result[i] = 0.0f;
    } else {
      float powM1 = pow(channel, m1);
      result[i] = pow((c1 + c2 * powM1) / (1.0f + c3 * powM1), m2);
    }
  }
  
  return result;
}

// Inverse PQ: Convert from PQ-encoded values back to linear (nits/10000)
// Useful for reading existing PQ textures or debugging
float3 pqToLinear(float3 pq)
{
  const float m1 = 0.1593017578125f;
  const float m2 = 78.84375f;
  const float c1 = 0.8359375f;
  const float c2 = 18.8515625f;
  const float c3 = 18.6875f;
  
  float3 result;
  for (int i = 0; i < 3; ++i) {
    float N = pq[i];
    if (N <= 0.0f) {
      result[i] = 0.0f;
    } else {
      float Np = pow(N, 1.0f / m2);
      float num = max(Np - c1, 0.0f);
      float den = c2 - c3 * Np;
      result[i] = pow(num / max(den, 1e-6f), 1.0f / m1);
    }
  }
  return result;
}


// HDR10 color space conversion (BT.2020 + PQ)
float3 convertToHDR10(float3 color) 
{
  // Convert from BT.709/sRGB to BT.2020 wide color gamut
  color = rec709ToRec2020(color);
  
  // For PQ: Input colors are assumed to be in relative [0-1] range where 1.0 = paper white
  // Scale to nits first: multiply by paper white to get actual nits, then clamp to display range
  float3 colorInNits = color * cb.hdrPaperWhiteLuminance;
  colorInNits = clamp(colorInNits, float3(cb.hdrMinLuminance), float3(cb.hdrMaxLuminance));
  
  // Normalize by PQ reference peak (10,000 nits) for PQ encoding
  const float PQ_MAX_NITS = 10000.0f;
  float3 normalizedColor = colorInNits / PQ_MAX_NITS;
  
  return linearToPQ(normalizedColor);
}

// Note: Old dithering functions removed - now using high-quality blue noise dithering

// Note: Complex Bayer pattern dithering functions removed - using superior blue noise approach

// Note: Removed complex PQ-specific dithering - blue noise handles this better

// Note: All complex manual dithering removed - using professional blue noise approach

// Note: Pre-tone-mapping dithering removed - blue noise handles this optimally

// ============================================================================
// Frostbite-style HDR Tonemapper
// Based on: https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
// Simplified implementation with natural highlight handling
// ============================================================================

// Frostbite exponential range compression (original algorithm)
// Maps [0, inf) -> [0, max_value) with linear segment below threshold
float frostbiteRangeCompress(float val, float threshold, float maxValue)
{
  if (val < threshold) {
    return val;  // Linear passthrough below threshold
  } else if (maxValue <= threshold) {
    return threshold;
  } else {
    float range = maxValue - threshold;
    // Exponential compression: asymptotically approaches maxValue
    return threshold + range * (1.0f - exp(-(val - threshold) / range));
  }
}

// Main Frostbite tonemapper (matching original algorithm)
// maxValue = peak_luminance / paper_white (HDR headroom ratio)
// rolloffStart = threshold for linear segment (default 0.25)
float3 frostbiteHDR(float3 color, float maxValue, float rolloffStart, float saturationMult, float hueCorrect)
{
  // Ensure positive input
  color = max(color, float3(0.0f));
  
  // Get input luminance
  float inputLuma = calcBt709Luminance(color);
  
  if (inputLuma < 1e-6f) {
    return float3(0.0f);
  }
  
  // ---- Step 1: Compress luminance ----
  // Uses Frostbite's exponential compression
  // Output range is [0, maxValue] where 1.0 = paper white
  float compressedLuma = frostbiteRangeCompress(inputLuma, rolloffStart, maxValue);
  
  // ---- Step 2: Scale color to match compressed luminance (hue-preserving) ----
  float scale = compressedLuma / inputLuma;
  float3 huePreserving = color * scale;
  
  // ---- Step 3: Per-channel compression ----
  // Helps with very saturated highlights but shifts hue
  float3 perChannel = float3(
    frostbiteRangeCompress(color.r, rolloffStart, maxValue),
    frostbiteRangeCompress(color.g, rolloffStart, maxValue),
    frostbiteRangeCompress(color.b, rolloffStart, maxValue)
  );
  
  // ---- Step 4: Blend hue-preserving and per-channel ----
  float3 blended = lerp(perChannel, huePreserving, hueCorrect);
  
  // ---- Step 5: Gamut mapping for values that went out of bounds ----
  // Clamp negative values, but allow values > 1.0 for HDR headroom
  blended = max(blended, float3(0.0f));
  
  // For very saturated colors that exceed maxValue, desaturate toward luminance
  float maxChannel = max(blended.r, max(blended.g, blended.b));
  if (maxChannel > maxValue) {
    // Desaturate to bring back into range
    float newLuma = calcBt709Luminance(blended);
    float3 gray = float3(newLuma);
    float t = (maxChannel - maxValue) / (maxChannel - newLuma + 1e-6f);
    t = saturate(t);
    blended = lerp(blended, gray, t);
  }
  
  // ---- Step 6: Saturation adjustment ----
  float finalLuma = calcBt709Luminance(blended);
  float3 result = lerp(float3(finalLuma), blended, saturationMult);
  
  // Ensure no negative values
  return max(result, float3(0.0f));
}

// ============================================================================
// ACES HDR Tonemapper
// ============================================================================

// ACES modified for HDR with configurable headroom
// maxValue = peak/paperwhite ratio for HDR output range
float3 acesHDR(float3 color, float maxValue)
{
  // Standard ACES input/output matrices
  const float3x3 aces_input_matrix = float3x3(
    0.59719f, 0.35458f, 0.04823f,
    0.07600f, 0.90834f, 0.01566f,
    0.02840f, 0.13383f, 0.83777f
  );
  
  const float3x3 aces_output_matrix = float3x3(
    1.60475f, -0.53108f, -0.07367f,
    -0.10208f, 1.10813f, -0.00605f,
    -0.00327f, -0.07276f, 1.07602f
  );
  
  // Scale input to account for HDR headroom
  // We want bright values to map to > 1.0 in output
  // ACES naturally compresses to [0,1], so we pre-scale by 1/maxValue
  // and post-scale by maxValue to use the full HDR range
  float inputScale = 1.0f / maxValue;
  color *= inputScale;
  
  // Apply input matrix transform
  color = mul(aces_input_matrix, color);
  
  // Apply RRT and ODT curve
  float3 a = color * (color + 0.0245786f) - 0.000090537f;
  float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
  color = a / b;
  
  // Apply output matrix transform
  color = mul(aces_output_matrix, color);
  
  // Scale back up for HDR output range [0, maxValue]
  color *= maxValue;
  
  // Clamp to valid HDR range
  return clamp(color, float3(0.0f), float3(maxValue));
}

// Shadow/Midtone/Highlight adjustments (log-luminance tri-band with smooth masks)
float3 applyShadowMidtoneHighlight(float3 color, float shadows, float midtones, float highlights)
{
  // Perceptual luminance
  const float eps = 1e-6f;
  float luma = calcBt709Luminance(color);
  float logLuma = log2(max(luma, eps));

  // Map log-luma from a reasonable HDR range [minStops, maxStops] to [0,1]
  // These boundaries roughly cover very dark shadows to bright highlights in relative space
  const float minStops = -10.0f; // extend shadow range
  const float maxStops =   8.0f; // extend highlight range to catch speculars
  float l01 = saturate((logLuma - minStops) / (maxStops - minStops));

  // Smooth tri-band masks with limited overlap
  // Tuned to give the highlights control more presence in typical content
  // - Shadows: strong below ~0.45
  // - Highlights: start earlier (~0.55) to ensure visible impact
  // - Midtones: fills remaining energy between the two
  float shadowsMask    = 1.0f - smoothstep(0.20f, 0.45f, l01);
  float highlightsMask =        smoothstep(0.55f, 0.75f, l01);
  float midtonesMask   = saturate(1.0f - shadowsMask - highlightsMask);

  // Normalize so masks sum to 1 where present to reduce unintended global shifts
  float maskSum = shadowsMask + midtonesMask + highlightsMask;
  if (maskSum > 0.0f) {
    float inv = rcp(maskSum);
    shadowsMask    *= inv;
    midtonesMask   *= inv;
    highlightsMask *= inv;
  }

  // Combine adjustments in EV space with the masks
  // Slightly bias highlights to improve perceptual response in narrow bright regions
  float ev = shadows    * shadowsMask
           + midtones   * midtonesMask
           + highlights * (highlightsMask * 1.15f);

  return color * exp2(ev);
}

// Main HDR tone mapping dispatch
float3 hdrToneMapping(float3 color, float maxLuminance, float paperWhite)
{
  // Apply exposure adjustment first (unless bypassing everything)
  if (cb.hdrToneMapper != 0) {
    color *= getExposure(InExposure, cb.enableAutoExposure != 0, cb.exposureFactor);
  }
  
  // Apply selected HDR tone mapping method
  switch (cb.hdrToneMapper) {
    case 0: // None - true bypass for debugging
      // Only apply minimal exposure if auto exposure is enabled
      if (cb.enableAutoExposure != 0) {
        color *= getExposure(InExposure, true, 1.0f);
      }
      return color; // No clamping to preserve original precision
    case 1: // ACES_HDR
      {
        float maxValue = maxLuminance / max(paperWhite, 1.0f);
        return acesHDR(color, maxValue);
      }
    case 2: // Frostbite
      // Frostbite parameters from user settings
      {
        float maxValue = cb.hdrMaxLuminance / max(cb.hdrPaperWhiteLuminance, 1.0f);
        return frostbiteHDR(color, maxValue, cb.frostbiteRolloffStart, cb.frostbiteSaturation, cb.frostbiteHueCorrect);
      }
    default:
      return color; // Default to no tone mapping if an unknown case is selected
  }
}

// ============================================================================
// Color Grading Functions
// ============================================================================

// Apply color balance (RGB multipliers)
float3 applyColorBalance(float3 color, float3 balance)
{
  return color * balance;
}

// Apply saturation adjustment
float3 applySaturation(float3 color, float saturation)
{
  float luma = calcBt709Luminance(color);
  return lerp(float3(luma), color, saturation);
}

// Apply contrast adjustment (pivot at 0.5 in relative space)
float3 applyContrast(float3 color, float contrast)
{
  // Use log-space contrast for better HDR handling
  const float pivot = 0.18f; // Middle gray
  float3 logColor = log2(max(color, float3(1e-6f)));
  float logPivot = log2(pivot);
  float3 adjusted = (logColor - logPivot) * contrast + logPivot;
  return exp2(adjusted);
}

// ============================================================================
// Debug Visualization Functions
// ============================================================================

// Luminance heatmap: visualize brightness levels
// Blue = dark (0-0.2), Green = mid (0.2-0.5), Yellow = bright (0.5-1.0), 
// Red = very bright (1.0-2.0), White = extremely bright (>2.0)
float3 luminanceHeatmap(float luminance)
{
  float3 result;
  
  if (luminance < 0.2f) {
    // Blue to cyan (dark region)
    float t = luminance / 0.2f;
    result = lerp(float3(0.0f, 0.0f, 0.3f), float3(0.0f, 0.5f, 1.0f), t);
  } else if (luminance < 0.5f) {
    // Cyan to green (mid-dark)
    float t = (luminance - 0.2f) / 0.3f;
    result = lerp(float3(0.0f, 0.5f, 1.0f), float3(0.0f, 1.0f, 0.0f), t);
  } else if (luminance < 1.0f) {
    // Green to yellow (mid-bright)
    float t = (luminance - 0.5f) / 0.5f;
    result = lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 1.0f, 0.0f), t);
  } else if (luminance < 2.0f) {
    // Yellow to red (bright)
    float t = (luminance - 1.0f) / 1.0f;
    result = lerp(float3(1.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), t);
  } else if (luminance < 5.0f) {
    // Red to white (very bright / HDR)
    float t = (luminance - 2.0f) / 3.0f;
    result = lerp(float3(1.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f), t);
  } else {
    // White with intensity for extremely bright
    result = float3(1.0f, 1.0f, 1.0f);
  }
  
  return result;
}

// Check if color is out of gamut (any channel negative or exceeds max)
bool isOutOfGamut(float3 color, float maxValue)
{
  return color.r < 0.0f || color.g < 0.0f || color.b < 0.0f ||
         color.r > maxValue || color.g > maxValue || color.b > maxValue;
}

// Main HDR processing function
float3 processHDR(float3 inputColor, uint2 pixelPosition)
{
  float3 color = inputColor;
  
  // Note: Pre-tone mapping dithering removed - now using high-quality blue noise dithering after processing
  
  // Apply HDR tone mapping (relative domain)
  color = hdrToneMapping(color, cb.hdrMaxLuminance, cb.hdrPaperWhiteLuminance);
  
  // Apply HDR-specific brightness and exposure adjustments (relative domain)
  // Apply exposure bias (in EV stops)
  color *= exp2(cb.hdrExposureBias);
  
  // Apply brightness multiplier
  color *= cb.hdrBrightness;
  
  // Apply shadow/midtone/highlight adjustments
  color = applyShadowMidtoneHighlight(color, cb.hdrShadows, cb.hdrMidtones, cb.hdrHighlights);
  
  // Apply color grading (color balance, saturation, contrast)
  float3 colorBalance = float3(cb.hdrColorBalanceR, cb.hdrColorBalanceG, cb.hdrColorBalanceB);
  color = applyColorBalance(color, colorBalance);
  color = applySaturation(color, cb.hdrSaturation);
  color = applyContrast(color, cb.hdrContrast);
  
  // Debug: Check for out-of-gamut before conversion
  float maxValue = cb.hdrMaxLuminance / max(cb.hdrPaperWhiteLuminance, 1.0f);
  bool outOfGamut = isOutOfGamut(color, maxValue);
  
  // Debug: Store luminance for heatmap before color space conversion
  float debugLuminance = calcBt709Luminance(color);
  
  // Ensure no negative values before color space conversion
  color = max(color, float3(0.0f));
  
  // Convert to HDR10 (BT.2020 + PQ)
  color = convertToHDR10(color);
  
  // Apply high-quality blue noise dithering AFTER color space conversion
  // This uses the same blue noise infrastructure as SDR for consistent quality
  color = ditherToHDR(
    color,
    pixelPosition,
    cb.frameIndex,
    cb.hdrEnableDithering != 0,
    true, // Enable temporal dithering for smoothest results
    0, // Reserved (HDR10/PQ is always used)
    cb.hdrBlueNoiseAmplitude
  );
  
  // Debug visualizations (applied after all processing)
  if (cb.hdrShowLuminanceHeatmap != 0) {
    // Show luminance heatmap - convert to HDR10 for display
    float3 heatmap = luminanceHeatmap(debugLuminance);
    // Convert heatmap to HDR10 output (BT.2020 + PQ)
    heatmap = rec709ToRec2020(heatmap);
    heatmap = heatmap * cb.hdrPaperWhiteLuminance / 10000.0f;
    color = linearToPQ(heatmap);
  } else if (cb.hdrShowGamutWarning != 0 && outOfGamut) {
    // Show out-of-gamut pixels in magenta
    float3 magenta = float3(1.0f, 0.0f, 1.0f);
    // Convert magenta to HDR10 output (BT.2020 + PQ)
    magenta = rec709ToRec2020(magenta);
    magenta = magenta * cb.hdrPaperWhiteLuminance / 10000.0f;
    color = linearToPQ(magenta);
  }
  
  return color;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  float4 inputColor = imageLoad(InColorBuffer, threadId);
  
  float3 processedColor = processHDR(inputColor.rgb, threadId);
  
  // Preserve alpha channel
  OutColorBuffer[threadId] = float4(processedColor, inputColor.a);
} 