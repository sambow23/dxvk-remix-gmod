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
#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"

// HDR Processing binding indices (using unique values)
#define HDR_PROCESSING_INPUT_BUFFER           0
#define HDR_PROCESSING_OUTPUT_BUFFER          1  
#define HDR_PROCESSING_EXPOSURE_INPUT         2

layout(rgba32f, binding = HDR_PROCESSING_INPUT_BUFFER)
RWTexture2D<float4> InColorBuffer;

layout(rgba32f, binding = HDR_PROCESSING_OUTPUT_BUFFER)
RWTexture2D<float4> OutColorBuffer;

layout(binding = HDR_PROCESSING_EXPOSURE_INPUT)
RWTexture1D<float> InExposure;

// HDR Processing Arguments
struct HDRProcessingArgs {
  uint enableAutoExposure;
  float hdrMaxLuminance;
  float hdrMinLuminance;
  float hdrPaperWhiteLuminance;
  float uiBrightness;
  uint hdrUseSceneReferred;
  float exposureFactor;
  uint frameIndex;
  uint hdrFormat;  // 0=Linear, 1=PQ, 2=HLG
  float hdrExposureBias;
  float hdrBrightness;
  uint hdrToneMapper;  // 0=None, 1=ACES_HDR
  uint hdrEnableDithering;
  float hdrShadows;
  float hdrMidtones;
  float hdrHighlights;
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

// Convert from linear RGB to HLG (Hybrid Log-Gamma) for HDR
float3 linearToHLG(float3 color) 
{
  // HLG constants (ITU-R BT.2100)
  const float a = 0.17883277f;
  const float b = 0.28466892f; // 1.0 - 4.0 * a
  const float c = 0.55991073f; // 0.5 - a * ln(4.0 * a)
  const float gamma = 1.2f;
  const float threshold = 1.0f / 12.0f;
  
  // Clamp input to valid range [0, 1] for HLG
  color = clamp(color, float3(0.0f), float3(1.0f));
  
  float3 result;
  for (int i = 0; i < 3; ++i) {
    float channel = color[i];
    if (channel <= 0.0f) {
      result[i] = 0.0f;
    } else if (channel <= threshold) {
      result[i] = sqrt(3.0f * channel);
    } else {
      result[i] = a * log(12.0f * channel - b) + c;
    }
  }
  
  // Apply system gamma with safety check
  return pow(max(result, float3(0.0f)), 1.0f / gamma);
}

// Enhanced color space conversion with HDR10 and HLG support
float3 convertColorSpace(float3 color, bool usePQ, bool useHLG) 
{
  // First convert to Rec.2020 color space
  color = rec709ToRec2020(color);
  
  // Apply appropriate transfer function
  if (usePQ) {
    // For PQ: Input colors are assumed to be in relative [0-1] range where 1.0 = paper white
    // Scale to nits first: multiply by paper white to get actual nits
    float3 colorInNits = color * cb.hdrPaperWhiteLuminance;
    // Then normalize by PQ reference peak (10,000 nits) for PQ encoding
    const float PQ_MAX_NITS = 10000.0f;
    float3 normalizedColor = colorInNits / PQ_MAX_NITS;
    return linearToPQ(normalizedColor);
  } else if (useHLG) {
    // For HLG: Input colors are assumed to be in relative [0-1] range where 1.0 = paper white
    // Scale to the display peak range for HLG
    float3 colorInNits = color * cb.hdrPaperWhiteLuminance;
    // HLG system gamma is designed for [0,1] input where 1.0 = display peak
    float3 normalizedColor = colorInNits / cb.hdrMaxLuminance;
    return linearToHLG(normalizedColor);
      } else {
      // Linear: Input colors are already in relative [0-1] range
      // For linear HDR, maintain the relative range with proper scaling for HDR displays
      // Scale to paper white luminance for proper HDR linear output
      return color;
    }
}

// Simple 3x3 random dither pattern
float getDitherValue(uint2 pixelCoord, uint frameIndex)
{
  // Blue noise would be better, but this simple pattern reduces banding
  const float3x3 ditherPattern = float3x3(
    0.0f, 0.75f, 0.25f,
    0.5f, 0.875f, 0.625f,
    0.125f, 0.375f, 1.0f
  );
  
  uint2 coord = (pixelCoord + frameIndex) % 3;
  return ditherPattern[coord.y][coord.x] / 1023.0f; // 10-bit dither range
}

// Improved dithering function with perceptually uniform distribution
float3 getPQDitherValue(uint2 pixelCoord, uint frameIndex, float3 color)
{
  // Use a larger dither pattern for better distribution
  const float dither8x8[64] = {
    0.0f / 64.0f, 32.0f / 64.0f, 8.0f / 64.0f, 40.0f / 64.0f, 2.0f / 64.0f, 34.0f / 64.0f, 10.0f / 64.0f, 42.0f / 64.0f,
    48.0f / 64.0f, 16.0f / 64.0f, 56.0f / 64.0f, 24.0f / 64.0f, 50.0f / 64.0f, 18.0f / 64.0f, 58.0f / 64.0f, 26.0f / 64.0f,
    12.0f / 64.0f, 44.0f / 64.0f, 4.0f / 64.0f, 36.0f / 64.0f, 14.0f / 64.0f, 46.0f / 64.0f, 6.0f / 64.0f, 38.0f / 64.0f,
    60.0f / 64.0f, 28.0f / 64.0f, 52.0f / 64.0f, 20.0f / 64.0f, 62.0f / 64.0f, 30.0f / 64.0f, 54.0f / 64.0f, 22.0f / 64.0f,
    3.0f / 64.0f, 35.0f / 64.0f, 11.0f / 64.0f, 43.0f / 64.0f, 1.0f / 64.0f, 33.0f / 64.0f, 9.0f / 64.0f, 41.0f / 64.0f,
    51.0f / 64.0f, 19.0f / 64.0f, 59.0f / 64.0f, 27.0f / 64.0f, 49.0f / 64.0f, 17.0f / 64.0f, 57.0f / 64.0f, 25.0f / 64.0f,
    15.0f / 64.0f, 47.0f / 64.0f, 7.0f / 64.0f, 39.0f / 64.0f, 13.0f / 64.0f, 45.0f / 64.0f, 5.0f / 64.0f, 37.0f / 64.0f,
    63.0f / 64.0f, 31.0f / 64.0f, 55.0f / 64.0f, 23.0f / 64.0f, 61.0f / 64.0f, 29.0f / 64.0f, 53.0f / 64.0f, 21.0f / 64.0f
  };
  
  uint2 coord = (pixelCoord + frameIndex) % 8;
  float ditherValue = dither8x8[coord.y * 8 + coord.x] - 0.5f;
  
  // Scale dither based on luminance - more dithering in darker areas where PQ is most sensitive
  float luminance = calcBt709Luminance(color);
  float adaptiveScale = 1.0f / (1.0f + luminance * 10.0f); // Reduce dithering in bright areas
  
  // For 10-bit output, use 1/1024 as base amplitude
  float3 dither = float3(ditherValue) * adaptiveScale / 1024.0f;
  
  return dither;
}

// Enhanced PQ-aware dithering with improved luminance sensitivity
float3 getEnhancedPQDither(uint2 pixelCoord, uint frameIndex, float3 color)
{
  // Use multiple dither patterns for temporal variation
  const float dither8x8_pattern1[64] = {
    0.0f / 64.0f, 32.0f / 64.0f, 8.0f / 64.0f, 40.0f / 64.0f, 2.0f / 64.0f, 34.0f / 64.0f, 10.0f / 64.0f, 42.0f / 64.0f,
    48.0f / 64.0f, 16.0f / 64.0f, 56.0f / 64.0f, 24.0f / 64.0f, 50.0f / 64.0f, 18.0f / 64.0f, 58.0f / 64.0f, 26.0f / 64.0f,
    12.0f / 64.0f, 44.0f / 64.0f, 4.0f / 64.0f, 36.0f / 64.0f, 14.0f / 64.0f, 46.0f / 64.0f, 6.0f / 64.0f, 38.0f / 64.0f,
    60.0f / 64.0f, 28.0f / 64.0f, 52.0f / 64.0f, 20.0f / 64.0f, 62.0f / 64.0f, 30.0f / 64.0f, 54.0f / 64.0f, 22.0f / 64.0f,
    3.0f / 64.0f, 35.0f / 64.0f, 11.0f / 64.0f, 43.0f / 64.0f, 1.0f / 64.0f, 33.0f / 64.0f, 9.0f / 64.0f, 41.0f / 64.0f,
    51.0f / 64.0f, 19.0f / 64.0f, 59.0f / 64.0f, 27.0f / 64.0f, 49.0f / 64.0f, 17.0f / 64.0f, 57.0f / 64.0f, 25.0f / 64.0f,
    15.0f / 64.0f, 47.0f / 64.0f, 7.0f / 64.0f, 39.0f / 64.0f, 13.0f / 64.0f, 45.0f / 64.0f, 5.0f / 64.0f, 37.0f / 64.0f,
    63.0f / 64.0f, 31.0f / 64.0f, 55.0f / 64.0f, 23.0f / 64.0f, 61.0f / 64.0f, 29.0f / 64.0f, 53.0f / 64.0f, 21.0f / 64.0f
  };
  
  const float dither8x8_pattern2[64] = {
    1.0f / 64.0f, 33.0f / 64.0f, 9.0f / 64.0f, 41.0f / 64.0f, 3.0f / 64.0f, 35.0f / 64.0f, 11.0f / 64.0f, 43.0f / 64.0f,
    49.0f / 64.0f, 17.0f / 64.0f, 57.0f / 64.0f, 25.0f / 64.0f, 51.0f / 64.0f, 19.0f / 64.0f, 59.0f / 64.0f, 27.0f / 64.0f,
    13.0f / 64.0f, 45.0f / 64.0f, 5.0f / 64.0f, 37.0f / 64.0f, 15.0f / 64.0f, 47.0f / 64.0f, 7.0f / 64.0f, 39.0f / 64.0f,
    61.0f / 64.0f, 29.0f / 64.0f, 53.0f / 64.0f, 21.0f / 64.0f, 63.0f / 64.0f, 31.0f / 64.0f, 55.0f / 64.0f, 23.0f / 64.0f,
    2.0f / 64.0f, 34.0f / 64.0f, 10.0f / 64.0f, 42.0f / 64.0f, 0.0f / 64.0f, 32.0f / 64.0f, 8.0f / 64.0f, 40.0f / 64.0f,
    50.0f / 64.0f, 18.0f / 64.0f, 58.0f / 64.0f, 26.0f / 64.0f, 48.0f / 64.0f, 16.0f / 64.0f, 56.0f / 64.0f, 24.0f / 64.0f,
    14.0f / 64.0f, 46.0f / 64.0f, 6.0f / 64.0f, 38.0f / 64.0f, 12.0f / 64.0f, 44.0f / 64.0f, 4.0f / 64.0f, 36.0f / 64.0f,
    62.0f / 64.0f, 30.0f / 64.0f, 54.0f / 64.0f, 22.0f / 64.0f, 60.0f / 64.0f, 28.0f / 64.0f, 52.0f / 64.0f, 20.0f / 64.0f
  };
  
  // Choose pattern based on frame index for temporal variation
  uint2 coord = (pixelCoord + frameIndex) % 8;
  bool usePattern2 = (frameIndex % 2) == 1;
  float ditherValue = usePattern2 ? 
    dither8x8_pattern2[coord.y * 8 + coord.x] - 0.5f :
    dither8x8_pattern1[coord.y * 8 + coord.x] - 0.5f;
  
  // Calculate luminance for PQ curve sensitivity analysis
  float luminance = calcBt709Luminance(color);
  float normalizedLuminance = luminance / cb.hdrMaxLuminance;
  
  // PQ curve sensitivity analysis - the curve is most sensitive at different points
  // Low luminance (shadows): Very sensitive to small changes
  // Mid luminance (midtones): Moderately sensitive  
  // High luminance (highlights): Less sensitive but still needs dithering for smooth gradients
  
  float pqSensitivity;
  if (normalizedLuminance < 0.1f) {
    // Shadows: High sensitivity, more dithering needed
    pqSensitivity = 2.5f;
  } else if (normalizedLuminance < 0.5f) {
    // Midtones: Moderate sensitivity but critical for visual quality
    pqSensitivity = 1.8f;
  } else if (normalizedLuminance < 0.8f) {
    // Upper midtones/highlights: Still needs significant dithering
    pqSensitivity = 1.5f;
  } else {
    // Bright highlights: Lower sensitivity but smooth gradients still important
    pqSensitivity = 1.2f;
  }
  
  // Base dither amplitude for 10-bit precision
  float baseAmplitude = 1.0f / 1024.0f;
  
  // Apply PQ sensitivity scaling
  float3 dither = float3(ditherValue) * baseAmplitude * pqSensitivity;
  
  return dither;
}

// Enhanced linear HDR dithering to match PQ quality
float3 getEnhancedLinearDither(uint2 pixelCoord, uint frameIndex, float3 color)
{
  // Use the same high-quality patterns as PQ mode
  const float dither8x8_pattern1[64] = {
    0.0f / 64.0f, 32.0f / 64.0f, 8.0f / 64.0f, 40.0f / 64.0f, 2.0f / 64.0f, 34.0f / 64.0f, 10.0f / 64.0f, 42.0f / 64.0f,
    48.0f / 64.0f, 16.0f / 64.0f, 56.0f / 64.0f, 24.0f / 64.0f, 50.0f / 64.0f, 18.0f / 64.0f, 58.0f / 64.0f, 26.0f / 64.0f,
    12.0f / 64.0f, 44.0f / 64.0f, 4.0f / 64.0f, 36.0f / 64.0f, 14.0f / 64.0f, 46.0f / 64.0f, 6.0f / 64.0f, 38.0f / 64.0f,
    60.0f / 64.0f, 28.0f / 64.0f, 52.0f / 64.0f, 20.0f / 64.0f, 62.0f / 64.0f, 30.0f / 64.0f, 54.0f / 64.0f, 22.0f / 64.0f,
    3.0f / 64.0f, 35.0f / 64.0f, 11.0f / 64.0f, 43.0f / 64.0f, 1.0f / 64.0f, 33.0f / 64.0f, 9.0f / 64.0f, 41.0f / 64.0f,
    51.0f / 64.0f, 19.0f / 64.0f, 59.0f / 64.0f, 27.0f / 64.0f, 49.0f / 64.0f, 17.0f / 64.0f, 57.0f / 64.0f, 25.0f / 64.0f,
    15.0f / 64.0f, 47.0f / 64.0f, 7.0f / 64.0f, 39.0f / 64.0f, 13.0f / 64.0f, 45.0f / 64.0f, 5.0f / 64.0f, 37.0f / 64.0f,
    63.0f / 64.0f, 31.0f / 64.0f, 55.0f / 64.0f, 23.0f / 64.0f, 61.0f / 64.0f, 29.0f / 64.0f, 53.0f / 64.0f, 21.0f / 64.0f
  };
  
  const float dither8x8_pattern2[64] = {
    1.0f / 64.0f, 33.0f / 64.0f, 9.0f / 64.0f, 41.0f / 64.0f, 3.0f / 64.0f, 35.0f / 64.0f, 11.0f / 64.0f, 43.0f / 64.0f,
    49.0f / 64.0f, 17.0f / 64.0f, 57.0f / 64.0f, 25.0f / 64.0f, 51.0f / 64.0f, 19.0f / 64.0f, 59.0f / 64.0f, 27.0f / 64.0f,
    13.0f / 64.0f, 45.0f / 64.0f, 5.0f / 64.0f, 37.0f / 64.0f, 15.0f / 64.0f, 47.0f / 64.0f, 7.0f / 64.0f, 39.0f / 64.0f,
    61.0f / 64.0f, 29.0f / 64.0f, 53.0f / 64.0f, 21.0f / 64.0f, 63.0f / 64.0f, 31.0f / 64.0f, 55.0f / 64.0f, 23.0f / 64.0f,
    2.0f / 64.0f, 34.0f / 64.0f, 10.0f / 64.0f, 42.0f / 64.0f, 0.0f / 64.0f, 32.0f / 64.0f, 8.0f / 64.0f, 40.0f / 64.0f,
    50.0f / 64.0f, 18.0f / 64.0f, 58.0f / 64.0f, 26.0f / 64.0f, 48.0f / 64.0f, 16.0f / 64.0f, 56.0f / 64.0f, 24.0f / 64.0f,
    14.0f / 64.0f, 46.0f / 64.0f, 6.0f / 64.0f, 38.0f / 64.0f, 12.0f / 64.0f, 44.0f / 64.0f, 4.0f / 64.0f, 36.0f / 64.0f,
    62.0f / 64.0f, 30.0f / 64.0f, 54.0f / 64.0f, 22.0f / 64.0f, 60.0f / 64.0f, 28.0f / 64.0f, 52.0f / 64.0f, 20.0f / 64.0f
  };
  
  // Choose pattern based on frame index for temporal variation
  uint2 coord = (pixelCoord + frameIndex) % 8;
  bool usePattern2 = (frameIndex % 2) == 1;
  float ditherValue = usePattern2 ? 
    dither8x8_pattern2[coord.y * 8 + coord.x] - 0.5f :
    dither8x8_pattern1[coord.y * 8 + coord.x] - 0.5f;
  
  // For linear mode, use appropriate precision scaling
  // Even though it's linear, we still need good dithering for smooth gradients
  float luminance = calcBt709Luminance(color);
  float normalizedLuminance = luminance / cb.hdrMaxLuminance;
  
  // Linear mode sensitivity - less aggressive than PQ but still significant
  float linearSensitivity;
  if (normalizedLuminance < 0.1f) {
    // Shadows: Still sensitive in linear mode
    linearSensitivity = 1.5f;
  } else if (normalizedLuminance < 0.5f) {
    // Midtones: Important for smooth gradients
    linearSensitivity = 1.2f;
  } else if (normalizedLuminance < 0.8f) {
    // Upper midtones/highlights: Moderate dithering
    linearSensitivity = 1.0f;
  } else {
    // Bright highlights: Light dithering
    linearSensitivity = 0.8f;
  }
  
  // Base dither amplitude - use higher precision for linear mode
  float baseAmplitude = 1.0f / 2048.0f; // 11-bit precision for linear
  
  // Apply linear sensitivity scaling
  float3 dither = float3(ditherValue) * baseAmplitude * linearSensitivity;
  
  return dither;
}

// Apply enhanced intermediate dithering before tone mapping
float3 applyPreToneMappingDither(float3 color, uint2 pixelPosition, uint frameIndex)
{
  // Enhanced precision-preserving dithering before tone mapping
  uint2 coord = (pixelPosition + frameIndex * 3) % 8;
  
  // Use triangular distribution for better perceptual quality
  float noise1 = getDitherValue(pixelPosition, frameIndex);
  float noise2 = getDitherValue(pixelPosition + uint2(1, 1), frameIndex + 1);
  float triangularNoise = (noise1 + noise2) * 0.5f - 0.5f;
  
  // Apply luminance-adaptive dithering amplitude
  float luminance = calcBt709Luminance(color);
  float normalizedLuminance = luminance / cb.hdrMaxLuminance;
  
  // More aggressive pre-dithering for gradient preservation
  float adaptiveAmplitude;
  if (normalizedLuminance < 0.1f) {
    adaptiveAmplitude = 1.0f / 2048.0f; // Higher precision for shadows
  } else if (normalizedLuminance < 0.5f) {
    adaptiveAmplitude = 1.0f / 3072.0f; // Good precision for midtones
  } else {
    adaptiveAmplitude = 1.0f / 4096.0f; // Fine precision for highlights
  }
  
  float3 preDither = float3(triangularNoise) * adaptiveAmplitude;
  
  return color + preDither;
}

// Precision-enhanced ACES modified for HDR range preservation
float3 acesHDR(float3 color, float maxLuminance, float paperWhite)
{
  // Use the exact same matrices as the working ACES implementation
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
  
  // Apply input matrix transform
  color = mul(aces_input_matrix, color);
  
  // Apply RRT and ODT (same as working implementation)
  float3 a = color * (color + 0.0245786f) - 0.000090537f;
  float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
  color = a / b;
  
  // Apply output matrix transform
  color = mul(aces_output_matrix, color);
  
  // Clamp to valid range - ACES already outputs in proper range for HDR
  // Don't scale by paper white here as it will cause dimming
  return saturate(color);
}

// Shadow/Midtone/Highlight adjustments
float3 applyShadowMidtoneHighlight(float3 color, float shadows, float midtones, float highlights)
{
  // Calculate luminance for weight computation
  float luminance = calcBt709Luminance(color);
  
  // Normalize luminance to [0, 1] range for weight calculation
  float normalizedLuminance = clamp(luminance / (luminance + 1.0f), 0.0f, 1.0f);
  
  // Calculate weights for shadows, midtones, and highlights using smooth transitions
  // Shadow weight: highest for dark areas, decreases for bright areas
  float shadowWeight = 1.0f - smoothstep(0.0f, 0.5f, normalizedLuminance);
  
  // Midtone weight: bell curve centered around 0.5
  float midtoneWeight = 1.0f - abs(normalizedLuminance - 0.5f) * 2.0f;
  midtoneWeight = smoothstep(0.0f, 1.0f, midtoneWeight);
  
  // Highlight weight: highest for bright areas, decreases for dark areas
  float highlightWeight = smoothstep(0.5f, 1.0f, normalizedLuminance);
  
  // Normalize weights so they sum to 1
  float totalWeight = shadowWeight + midtoneWeight + highlightWeight;
  if (totalWeight > 0.0f) {
    shadowWeight /= totalWeight;
    midtoneWeight /= totalWeight;
    highlightWeight /= totalWeight;
  }
  
  // Apply adjustments as exposure changes (in EV stops)
  float shadowAdjustment = exp2(shadows * shadowWeight);
  float midtoneAdjustment = exp2(midtones * midtoneWeight);
  float highlightAdjustment = exp2(highlights * highlightWeight);
  
  // Combine all adjustments
  float totalAdjustment = shadowAdjustment * midtoneAdjustment * highlightAdjustment;
  
  return color * totalAdjustment;
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
      return acesHDR(color, maxLuminance, paperWhite);
    default:
      return color; // Default to no tone mapping if an unknown case is selected
  }
}

// UI brightness adjustment for orthographic/UI elements
float3 adjustUIBrightness(float3 color, float brightnessFactor)
{
  // Simple brightness adjustment for UI elements
  // Preserves color relationships while scaling brightness
  return color * brightnessFactor;
}

// Main HDR processing function
float3 processHDR(float3 inputColor, uint2 pixelPosition)
{
  float3 color = inputColor;
  
  // Apply pre-tone mapping dithering to prevent quantization artifacts
  if (cb.hdrEnableDithering != 0) {
    color = applyPreToneMappingDither(color, pixelPosition, cb.frameIndex);
  }
  
  // Apply HDR tone mapping
  color = hdrToneMapping(color, cb.hdrMaxLuminance, cb.hdrPaperWhiteLuminance);
  
  // Apply HDR-specific brightness and exposure adjustments
  // Apply exposure bias (in EV stops)
  color *= exp2(cb.hdrExposureBias);
  
  // Apply brightness multiplier
  color *= cb.hdrBrightness;
  
  // Apply shadow/midtone/highlight adjustments
  color = applyShadowMidtoneHighlight(color, cb.hdrShadows, cb.hdrMidtones, cb.hdrHighlights);
  
  // Clamp to valid HDR range after adjustments
  color = clamp(color, float3(cb.hdrMinLuminance), float3(cb.hdrMaxLuminance));
  
  // Enhanced color space conversion with HDR format support
  bool usePQ = (cb.hdrFormat == 1);  // PQ/HDR10
  bool useHLG = (cb.hdrFormat == 2); // HLG
  color = convertColorSpace(color, usePQ, useHLG);
  
  // Apply dithering AFTER color space conversion for PQ
  if (cb.hdrEnableDithering != 0) {
    if (usePQ) {
      // Use enhanced PQ-aware dithering with improved sensitivity
      float3 dither = getEnhancedPQDither(pixelPosition, cb.frameIndex, color);
      color += dither;
    } else {
      // Use enhanced linear dithering for non-PQ formats
      float3 dither = getEnhancedLinearDither(pixelPosition, cb.frameIndex, color);
      color += dither;
    }
  }
  
  // Note: UI brightness adjustment would be applied in composite pass
  // based on detection of UI textures
  
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