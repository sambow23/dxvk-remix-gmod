/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

//!variant debug_view.comp
//!>       ENABLE_DEBUG_VIEW_OPTIONAL_FEATURES=0

//!variant debug_view_using_optional_extensions.comp
//!>       ENABLE_DEBUG_VIEW_OPTIONAL_FEATURES=1

//!end-variants

#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx/pass/debug_view/debug_view_binding_indices.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/algorithm/volume_composite_helpers.slangh"

#include "rtxdi/Reservoir.slangh"
#include "rtx/pass/nrc/nrc_utilities.slangh"

uint32_t RAB_EncodeNormal(float3 normal)
{
  return float2x32ToSnorm2x16(sphereDirectionToSignedOctahedral(normal));
}

float3 RAB_DecodeNormal(uint encodedNormal)
{
  return signedOctahedralToSphereDirection(snorm2x16ToFloat2x32(encodedNormal));
}

// Inputs

layout(binding = DEBUG_VIEW_BINDING_CONSTANTS_INPUT)
ConstantBuffer<DebugViewArgs> cb;

layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedPrimaryDirectDiffuseRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedPrimaryDirectSpecularRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedSecondaryCombinedDiffuseRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedSecondaryCombinedSpecularRadianceHitT;
layout(r16ui, binding = DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT)
Texture2D<uint> SharedFlags;
layout(r32f, binding = DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
Texture2D<float> PrimaryLinearViewZ;
layout(rgba16, binding = DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT)
Texture2D<float4> PrimaryVirtualWorldNormalPerceptualRoughness;
layout(rgba16, binding = DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT)
Texture2D<float4> PrimaryVirtualMotionVector;
layout(rg16, binding = DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT)
Texture2D<float2> PrimaryScreenSpaceMotionVector;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT)
Texture2D<float4> RtxdiConfidence;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT)
Texture2D<float4> FinalShading;
layout(r32ui, binding = DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT)
Texture2D<uint> Instrumentation;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_TERRAIN_INPUT)
Texture2D<float4> TerrainTexture;
layout(rgba32ui, binding = DEBUG_VIEW_BINDING_VOLUME_RESERVOIRS_INPUT)
Texture3D<uint4> VolumeReservoirs;

layout(binding = DEBUG_VIEW_BINDING_VOLUME_AGE_INPUT)
Sampler3D<float> VolumeFilteredRadianceAge;
layout(binding = DEBUG_VIEW_BINDING_VOLUME_RADIANCE_Y_INPUT)
Sampler3D<float4> VolumeFilteredRadianceY;
layout(binding = DEBUG_VIEW_BINDING_VOLUME_RADIANCE_COCG_INPUT)
Sampler3D<float2> VolumeFilteredRadianceCoCg;

layout(binding = DEBUG_VIEW_BINDING_VALUE_NOISE_SAMPLER)
Sampler2D<float4> ValueNoiseSampler;

layout(binding = DEBUG_VIEW_BINDING_BLUE_NOISE_TEXTURE)
Texture2DArray BlueNoise;

layout(rgba32f, binding = DEBUG_VIEW_BINDING_DEBUG_VIEW_INPUT)
Texture2D<float4> DebugView;

layout(rgba8f, binding = DEBUG_VIEW_BINDING_NRD_VALIDATION_LAYER_INPUT)
Texture2D<float4> NrdValidationLayer;


// Inputs / Outputs

layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformRedInputOutput;
layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformGreenInputOutput;
layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformBlueInputOutput;

layout(rgba16f, binding = DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT_OUTPUT)
RWTexture2D<float4> CompositeOutput;

layout(rgba32f, binding = DEBUG_VIEW_BINDING_PREVIOUS_FRAME_INPUT_OUTPUT)
RWTexture2D<float4> PreviousFrameDebugView;

// Outputs

layout(binding = DEBUG_VIEW_BINDING_STATISTICS_BUFFER_OUTPUT)
RWStructuredBuffer<float> DebugViewStatistics;

layout(rgba32f, binding = DEBUG_VIEW_BINDING_OUTPUT)
RWTexture2D<float4> DebugViewOutput;



// Samplers

layout(binding = DEBUG_VIEW_BINDING_NEAREST_SAMPLER)
SamplerState NearestSampler;

layout(binding = DEBUG_VIEW_BINDING_LINEAR_SAMPLER)
SamplerState LinearSampler;
#include "rtx/utility/common.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/external/NRD.slangh"

float4 sampleTexture(Texture2D<float4> texture, ivec2 threadId)
{
  const float2 uv = (threadId + 0.5f) / float2(cb.debugViewResolution);
  float4 sampledColor = float4(0.0f);

  // Note: sampledColor variable written to rather than returning from each of these cases directly as for
  // some reason directly causes a crash when attempting to execute this shader on AMD's (current) drivers
  // (may be fixed at some point though).
  switch(cb.samplerType)
  {
  case DebugViewSamplerType::Nearest:
    sampledColor = texture[threadId];
  case DebugViewSamplerType::NormalizedNearest:
    sampledColor = texture.SampleLevel(NearestSampler, uv, 0.0f);
  case DebugViewSamplerType::NormalizedLinear:
    sampledColor = texture.SampleLevel(LinearSampler, uv, 0.0f);
  }

  return sampledColor;
}

float nrdGetHitT(float normalizedHitT, float primaryHitPerceptualRoughness, float linearViewZ)
{
  if (cb.nrd.isReblurEnabled > 0)
  {
    return REBLUR_GetHitDist(normalizedHitT, linearViewZ, cb.nrd.hitDistanceParams, primaryHitPerceptualRoughness);
  }
  else
  {
    return normalizedHitT;
  }
}

vec4 nrdDenoisedHitTtoColor(ivec2 threadId)
{
  float normalizedHitT;
  float primaryHitPerceptualRoughness;

  switch (cb.debugViewIdx)
  {
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    normalizedHitT = DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].w;
    primaryHitPerceptualRoughness = 1.0;
    break;
  
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
    normalizedHitT = DenoisedPrimaryDirectSpecularRadianceHitT[threadId].w;
    primaryHitPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
    break;

  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    normalizedHitT = DebugView[threadId].w;
    primaryHitPerceptualRoughness = 1.0;
    break;

  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    normalizedHitT = DebugView[threadId].w;
    primaryHitPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
    break;
  }

  if (cb.nrd.isReblurEnabled > 0)
  {
    float linearViewZ = PrimaryLinearViewZ[threadId].x;
    return vec4(vec3(REBLUR_GetHitDist(normalizedHitT, linearViewZ, cb.nrd.hitDistanceParams, primaryHitPerceptualRoughness)), 1.0);
  }
  else
  {
    return vec4(vec3(normalizedHitT), 1.0);
  }
}

vec4 unormVectorToColor(vec3 unormVector)
{
  return vec4(unormVector * 2.0 - 1.0, 1.0); 
}

// Color codes a value into BGR, with only one channel being used to color code at a time
// Black: 0 value
// Red: values >= than max value
// Blue/Green (0,1]: the remaining values, with blue representing the lower half of valid range and green the rest
vec3 colorCodeIntoBGRexclusive(uint value, uint maxValue)
{
  if (value == 0)
  {
    return vec3(0);
  }
  else if (value >= maxValue)
  {
    return vec3(1, 0, 0);   
  }

  // Color code the rest, [1, maxValue-1], to a non-black BG color, one channel at a time

  value -= 1;
  const uint numValues = maxValue - 1;

  const float numChannels = 2;
  const uint ceilValuesPerChannel = ceil(numValues / numChannels);

  vec3 color = 0;
  const uint channel = value / ceilValuesPerChannel;
  const uint numValuesInCurrentChannel = min(numValues - channel * ceilValuesPerChannel, ceilValuesPerChannel);

  // Color code as (0,1]
  color[channel] = (float(value % numValuesInCurrentChannel) + 1) / numValuesInCurrentChannel;

  // Reswizzle to show lowest values in Blue and highest in Red
  return color.bgr;
}

vec4 loadInput(inout RNG randomState, ivec2 threadId)
{
  vec4 value = vec4(0);
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);

  // Compute the coordinate with respect to the upscaled output
  // Note: This is used so textures (primarily just the final output) using the upscaled extent
  // rather than the downscaled extent can be displayed through the current debug view system.
  // This does mean that the image will be downscaled in a fairly poor nearest neighbor
  // way into the debug view texture, meaning when viewing the final output through the debug
  // view it is recommended to disable DLSS (or whatever other upscaler) until this is changed
  // to something better (the debug view texture ideally should be at the full output resolution).

  uvec2 upscaledResolution;
  FinalShading.GetDimensions(upscaledResolution.x, upscaledResolution.y);

  const ivec2 upscaledPixelCoordinate = (threadId * upscaledResolution) / cb.debugViewResolution;

  // Get the respective input based on the debug index

  switch (cb.debugViewIdx)
  {
  case DEBUG_VIEW_VIRTUAL_SHADING_NORMAL:
    value = unormVectorToColor(PrimaryVirtualWorldNormalPerceptualRoughness[threadId].xyz);
    break;
  case DEBUG_VIEW_VIRTUAL_MOTION_VECTOR:
    value = vec4(abs(PrimaryVirtualMotionVector[threadId].xyz), 1);
    break;
  case DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR:
    value = vec4(abs(PrimaryScreenSpaceMotionVector[threadId].xy), 0, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    value = vec4(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    value = vec4(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    value = vec4(DebugView[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    value = vec4(DebugView[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    value = vec4(DenoisedSecondaryCombinedDiffuseRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    value = vec4(DenoisedSecondaryCombinedSpecularRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    value = nrdDenoisedHitTtoColor(threadId);
    break;
  case DEBUG_VIEW_PRE_TONEMAP_OUTPUT:
  case DEBUG_VIEW_POST_TONEMAP_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT:
    value = FinalShading[upscaledPixelCoordinate];
    break;
  case DEBUG_VIEW_COMPOSITE_OUTPUT:
    value = CompositeOutput[threadId];
    break;
  case DEBUG_VIEW_VIEW_MODEL:
    if (geometryFlags.isViewModel)
    {
      value = FinalShading[upscaledPixelCoordinate];
    }
    else
    {
      // Checkerboard background
      const bool state1 = ((threadId.x >> 3) & 1) ^ ((threadId.y >> 3) & 1);
      value = vec4(0.25, 0.25, 0.25, 1) + state1 * vec4(0.25, 0.25, 0.25, 0);
    }
    break;
  case DEBUG_VIEW_PSR_PRIMARY_SECONDARY_SURFACE_MASK:
    value = geometryFlags.primarySelectedIntegrationSurface ? vec4(1, 1, 1, 1) : vec4(0, 0, 0, 1);
    break;
  case DEBUG_VIEW_TERRAIN_MAP:
    value = vec4(sampleTexture(TerrainTexture, threadId).rgb, 1);
    break;
  case DEBUG_VIEW_TERRAIN_MAP_OPACITY:
    value = vec4(sampleTexture(TerrainTexture, threadId).aaa, 1);
    break;
  case DEBUG_VIEW_NRC_UPDATE_RADIANCE:
  case DEBUG_VIEW_NRC_UPDATE_THROUGHPUT:
  case DEBUG_VIEW_NRC_UPDATE_RADIANCE_MULTIPLIED_BY_THROUGHPUT:
  case DEBUG_VIEW_NRC_UPDATE_IS_UNBIASED:
  case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_BOUNCES:
  case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_PATH_SEGMENTS:
  case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_INDIRECT_PATH_SEGMENTS:
    value = DebugView[Nrc::calculateTrainingPixelCoordinate(threadId, cb.nrcArgs)];
    break;

  case DEBUG_VIEW_EXPOSURE_HISTOGRAM:
    // Show histogram or weight as a bar across the top on the screen, technically not safe, but the first handful of threads should be fine.
    if (threadId.y < 32 && threadId.x < cb.debugViewResolution.x * 0.5)
    {
      const uint histogramBucket = (float(threadId.x) / (cb.debugViewResolution.x * 0.5)) * 256;
      vec4 histogramOrWeight = DebugView[uint2(histogramBucket, 0)];
      if (threadId.y < 16)
      {
        // Show weight
        value = float4(0,histogramOrWeight.yz,0);
      }
      else
      {
        // Show weighted histogram
        value = histogramOrWeight.y > 0 ? float4(0,1,0,0) : histogramOrWeight.xxxx;
      }
      
      if (histogramBucket == 0 && value.x > 0)
      {
        value = float4(1, 0, 0, 0); // Red, we have pixels that are in the ignore bucket.
      }
    }
    else
    {
      value = FinalShading[upscaledPixelCoordinate];
    }
    break;
  case DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE:
    value = float4(Instrumentation[threadId], 0, 0, 0);
    break;
  case DEBUG_VIEW_RTXDI_CONFIDENCE:
    if (cb.isRTXDIConfidenceValid) 
    {
      const float confidence = saturate(1.0 - RtxdiConfidence[threadId].x);
      const float confidenceAlpha = pow(confidence, 0.5);
      const float3 confidenceColor = turboColormap(confidence);
      const float3 sceneColor = FinalShading[upscaledPixelCoordinate].rgb;
      value.rgb = lerp(sceneColor, confidenceColor, confidenceAlpha);
      value.a = 1.0;
    }
    break;
  case DEBUG_VIEW_VOLUME_RESERVOIR_DEPTH_LAYERS: {
    const Camera camera = cb.camera;
    VolumeArgs volumeArgs = cb.volumeArgs;
    const uvec3 froxelDimensions = uvec3(volumeArgs.restirFroxelGridDimensions, volumeArgs.restirFroxelDepthSlices);
      
    vec2 screenUV = cameraPixelCoordinateToScreenUV(camera, threadId.xy);
    screenUV.x *= cb.volumeArgs.numFroxelVolumes;
    const float froxelVolume = floor(screenUV.x);
    screenUV.x = frac(screenUV.x);
    const uint widthTiles = ceil(sqrt(float(volumeArgs.restirFroxelDepthSlices)));
    const uint heightTiles = ceil(float(volumeArgs.restirFroxelDepthSlices) / widthTiles);
    const uint previewLevel = uint(screenUV.x * widthTiles) + uint(screenUV.y * heightTiles) * widthTiles;

    VolumeReSTIR_Reservoir reservoir = VolumeReSTIR_Reservoir::createEmpty();

    if (previewLevel < volumeArgs.restirFroxelDepthSlices)
    {
      vec2 previewUV = mod(screenUV, vec2(1.0f / widthTiles, 1.0f / heightTiles)) * vec2(widthTiles, heightTiles);
      previewUV.x = clamp(previewUV.x, volumeArgs.minFilteredRadianceU, volumeArgs.maxFilteredRadianceU);
      previewUV.x = (previewUV.x + froxelVolume) * volumeArgs.inverseNumFroxelVolumes;
      const float previewW = float(previewLevel) / float(volumeArgs.restirFroxelDepthSlices);

      VolumeReSTIR_PackedReservoir packedReservoir;
      packedReservoir.data0 = VolumeReservoirs[uint3(vec3(previewUV, previewW) * froxelDimensions)];
      reservoir = VolumeReSTIR_Reservoir::createFromPacked(packedReservoir);
      if(cb.debugKnob.x <= 1.f)
      {
        value = reservoir.sampleCount;
      }
      else if(cb.debugKnob.x <= 2.f)
      {
        value = reservoir.lightSample.lightIndex;
      }
      else if(cb.debugKnob.x <= 3.f)
      {
        value = reservoir.weightSum;
      }
      else if(cb.debugKnob.x <= 4.f)
      {
        value = reservoir.lightSample.targetPdf;
      }
      else if(cb.debugKnob.x <= 5.f)
      {
        value = reservoir.knownVisible ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
      }
      else 
      {
        value.xy = float2(reservoir.lightSample.lightSampleCoordinates);
      }
    }

    break;
  }
  case DEBUG_VIEW_VOLUME_PREINTEGRATION: {
    const Camera camera = cb.camera;
    const VolumeArgs volumeArgs = cb.volumeArgs;
      
    const vec2 screenUV = cameraPixelCoordinateToScreenUV(camera, threadId.xy);
    const CameraDirections primaryRayDirections = cameraPixelCoordinateToDirection(camera, threadId.xy);
    
    // Calculate volumetric radiance and attenuation if volumetric lighting is enabled

    vec3 volumetricPreintegratedRadiance = vec3(0.0f, 0.0f, 0.0f);
    vec3 volumeAttenuation = vec3(1.0f, 1.0f, 1.0f);
    float volumetricIntegrationOffset = 0.f;
    integrateVolumetricNEE(
      randomState, VolumeFilteredRadianceAge, VolumeFilteredRadianceY, VolumeFilteredRadianceCoCg,
      volumeArgs, cb.animationTimeSec * 1000, threadId.xy, screenUV, false, cameraGetWorldPosition(camera), volumeArgs.froxelMaxDistance, primaryRayDirections.worldDirection,
      volumetricPreintegratedRadiance, volumeAttenuation);
    value.xyz = volumetricPreintegratedRadiance;
    break;
  }
  case DEBUG_VIEW_SCROLLING_LINE:
    value = FinalShading[upscaledPixelCoordinate];
    if (threadId.x == (cb.frameIdx + 10) % cb.debugViewResolution.x)
    {
      value = float4(0, 1, 1, 1);
    }
    break;
  case DEBUG_VIEW_NAN:
    bool isValid = DebugView[threadId].x != 0.0;

    // DEBUG_VIEW_VIRTUAL_SHADING_NORMAL
    isValid &= isValidValue(unormVectorToColor(PrimaryVirtualWorldNormalPerceptualRoughness[threadId].xyz));
    // DEBUG_VIEW_VIRTUAL_MOTION_VECTOR
    isValid &= isValidValue(PrimaryVirtualMotionVector[threadId].xy);
    // DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR
    isValid &= isValidValue(PrimaryScreenSpaceMotionVector[threadId].xy);
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(DebugView[threadId].xyz);
    // DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE
    isValid &= isValidValue(DenoisedSecondaryCombinedDiffuseRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE
    isValid &= isValidValue(DenoisedSecondaryCombinedSpecularRadianceHitT[threadId].xyz);

    float linearViewZ = PrimaryLinearViewZ[threadId].x;
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(nrdGetHitT(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].w, 1.0, linearViewZ));
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(nrdGetHitT(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].w, PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w, linearViewZ));
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(nrdGetHitT(DebugView[threadId].w, 1.0, linearViewZ));
    // DEBUG_VIEW_PRE_TONEMAP_OUTPUT
    // DEBUG_VIEW_POST_TONEMAP_OUTPUT
    // DEBUG_VIEW_VIEW_MODEL
    // DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT
    isValid &= isValidValue(FinalShading[upscaledPixelCoordinate]);
    // DEBUG_VIEW_COMPOSITE_OUTPUT
    isValid &= isValidValue(CompositeOutput[threadId]);

    // Skipped
    // DEBUG_VIEW_VIEW_MODEL
    // DEBUG_VIEW_PSR_PRIMARY_SECONDARY_SURFACE_MASK
    // DEBUG_VIEW_TERRAIN_MAP
    // DEBUG_VIEW_TERRAIN_MAP_OPACITY

    // Skipped - since they read in data from DebugView
    // DEBUG_VIEW_NRC_UPDATE_RADIANCE
    // DEBUG_VIEW_NRC_UPDATE_THROUGHPUT
    // DEBUG_VIEW_NRC_UPDATE_RADIANCE_MULTIPLIED_BY_THROUGHPUT
    // DEBUG_VIEW_NRC_UPDATE_IS_UNBIASED
    // DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_BOUNCES
    // DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_PATH_SEGMENTS
    // DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_INDIRECT_PATH_SEGMENTS

    // Skipped
    // DEBUG_VIEW_EXPOSURE_HISTOGRAM

    // DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE
    isValid &= isValidValue(Instrumentation[threadId]);
    
    // DEBUG_VIEW_RTXDI_CONFIDENCE
    if (cb.isRTXDIConfidenceValid) 
    {
      isValid &= isValidValue(RtxdiConfidence[threadId].x);
    }

    // Skipped
    // DEBUG_VIEW_VOLUME_RESERVOIR_DEPTH_LAYERS
    // DEBUG_VIEW_VOLUME_PREINTEGRATION
    // DEBUG_VIEW_SCROLLING_LINE

    // If there is invalid number, output NaN to trigger NaN highlight effect 
    value = isValid ? 1.0 : 0.0 / 0.0;

    break;

  case DEBUG_VIEW_SSS_DIFFUSION_PROFILE_SAMPLING:
  {
    value = DebugView[threadId] + FinalShading[upscaledPixelCoordinate];
    break;
  }
  case DEBUG_VIEW_NRD_INSTANCE_0_VALIDATION_LAYER:
  case DEBUG_VIEW_NRD_INSTANCE_1_VALIDATION_LAYER:
  case DEBUG_VIEW_NRD_INSTANCE_2_VALIDATION_LAYER:
  {
    float4 validation = NrdValidationLayer[threadId];
    value.xyz = lerp( FinalShading[upscaledPixelCoordinate].xyz, validation.xyz, validation.w );

    break;
  }
  default:
    value = DebugView[threadId];
    break;
  }

  // Quantize the input value to the requested step size if enabled
  // Note: This is done to allow for simulation of what the result roughly would look like if written to a quantized texture (e.g. to simulate 8 bits per channel,
  // quantize with a step size of 1/255) primarily to visualize banding artifacts better without the often times larger floating point precision of the input
  // textures hiding them.

  if (cb.enableInputQuantization)
  {
    value = round(value * cb.quantizationInverseStepSize) * cb.quantizationStepSize;
  }

  return value;
}

void hdrWaveformOutput(ivec2 threadId, RWTexture2D<uint> HDRWaveformInputOutput, float normalizedOutCoordinate)
{
  // Early out if the coordinate is out of range
  // Note: Using >= 1 as the coordinate value is floored, meaning a value of 1 would be one pixel outside the output texture.

  if (normalizedOutCoordinate >= 1.0f || normalizedOutCoordinate < 0.0f)
  {
    return;
  }

  const uvec2 outCoordinate = uvec2(
    uint(threadId.x / cb.hdrWaveformResolutionScaleFactor),
    uint(normalizedOutCoordinate * cb.hdrWaveformResolution.y));

  uint dummyOldValue;
  InterlockedAdd(HDRWaveformInputOutput[outCoordinate], 1, dummyOldValue);
}

void postprocess(inout RNG randomState, ivec2 threadId, inout vec4 outValue)
{
  // Color code inf, nan values
  if (cb.enableInfNanViewFlag)
  { 
    bool nanDetected = false;
    bool infDetected = false;
    
    // Detect nans and infs in pixel neighborhood
    if (cb.colorCodeRadius > 0) 
    {
      int iRadius = cb.colorCodeRadius;
      for (int y = threadId.y - iRadius; y <= threadId.y + iRadius; y++ ) {
        for (int x = threadId.x - iRadius; x <= threadId.x + iRadius; x++ ) {
          if (any(ivec2(x, y) >= cb.debugViewResolution) || any(ivec2(x, y) < 0)) continue;
          const vec4 temp = loadInput(randomState, ivec2(x, y));

          nanDetected |= any(isnan(temp));
          infDetected |= any(isinf(temp));
        }
      }
    }

    if (nanDetected || infDetected)
    {
      // Note: Override usual warning colors when pseudo color mode is in use as its visualization will not
      // display these properly.
      const bool usePseudoColorWarningColors =
        (cb.displayType == DebugViewDisplayType::Standard) &&
        (cb.pseudoColorMode != PseudoColorMode::Disabled);
      const vec3 kNanWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(1, 0, 0);
      const vec3 kInfWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(0, 0, 1);
      const vec3 kCombinedWarningColor =
        select(nanDetected, kNanWarningColor, vec3(0, 0, 0)) +
        select(infDetected, kInfWarningColor, vec3(0, 0, 0));

      outValue = cb.animationTimeSec > 0 
                ? vec4(sin(cb.animationTimeSec * 8) * kCombinedWarningColor, 1) // flash warning color
                : vec4(kCombinedWarningColor, 1);
      return;
    }
  }

  // Switch between Display mode

  if (cb.displayType == DebugViewDisplayType::Standard)
  {
    outValue *= cb.scale;
    outValue = (outValue - vec4(cb.minValue)) / (vec4(cb.maxValue - cb.minValue));

    if (!cb.enableAlphaChannelFlag)
    {
      outValue.a = 1;
    }

    if (cb.pseudoColorMode != PseudoColorMode::Disabled)
    {
      // Determine the input value to colormap based on the specified mode

      float pseudoColorInput;

      switch (cb.pseudoColorMode)
      {
      case PseudoColorMode::Luminance:
        pseudoColorInput = calcBt709Luminance(outValue.rgb);
        break;
      case PseudoColorMode::Red:
        pseudoColorInput = outValue.r;
        break;
      case PseudoColorMode::Green:
        pseudoColorInput = outValue.g;
        break;
      case PseudoColorMode::Blue:
        pseudoColorInput = outValue.b;
        break;
      case PseudoColorMode::Alpha:
        pseudoColorInput = outValue.a;
        break;
      }

      outValue.rgb = turboColormap(pseudoColorInput);
    }

    if (cb.enableGammaCorrectionFlag)
    {
      outValue.rgb = linearToGamma(outValue.rgb);
    }
  }
  else if (cb.displayType == DebugViewDisplayType::BGRExclusiveColor)
  {
    outValue.rgb = colorCodeIntoBGRexclusive(outValue.r, cb.maxValue);
  }
  else if (cb.displayType == DebugViewDisplayType::EV100)
  {
    const float luminance = calcBt709Luminance(outValue.rgb);
    const float ev100 = calcLuminanceEV100(luminance);
    const float normalizedEV = float(ev100 - cb.evMinValue) / cb.evRange;

    outValue = vec4(turboColormap(normalizedEV), 1.0f);
  }
  else if (cb.displayType == DebugViewDisplayType::HDRWaveform)
  {
    vec3 log10OutValue;

    if (cb.enableLuminanceModeFlag)
    {
      log10OutValue = log10(calcBt709Luminance(outValue.rgb));
    }
    else
    {
      log10OutValue = log10(outValue.rgb);
    }

    const vec3 normalizedOutCoordinate = (log10OutValue - cb.log10MinValue) / cb.log10Range;

    hdrWaveformOutput(threadId, HDRWaveformRedInputOutput, normalizedOutCoordinate.r);
    hdrWaveformOutput(threadId, HDRWaveformGreenInputOutput, normalizedOutCoordinate.g);
    hdrWaveformOutput(threadId, HDRWaveformBlueInputOutput, normalizedOutCoordinate.b);
  }
}

f16vec4 lerpFp16(f16vec4 a, f16vec4 b, float16_t lerpFactor)
{
  return a * (1.h - lerpFactor) + b * lerpFactor;
}

void storeDebugViewOutputStatistics(vec4 value)
{
  if (!cb.calculateStatistics)
  {
    return;
  }

#if ENABLE_DEBUG_VIEW_OPTIONAL_FEATURES
  // Optional features guarantees float atomics support

#define INTERLOCKED_OP_VEC4(INTERLOCKED_OP, STRUCTURED_BUFFER_FLOAT, vec4Value) \
  INTERLOCKED_OP(STRUCTURED_BUFFER_FLOAT[0], vec4Value.x);  \
  INTERLOCKED_OP(STRUCTURED_BUFFER_FLOAT[1], vec4Value.y);  \
  INTERLOCKED_OP(STRUCTURED_BUFFER_FLOAT[2], vec4Value.z);  \
  INTERLOCKED_OP(STRUCTURED_BUFFER_FLOAT[3], vec4Value.w)

  switch (cb.statisticsMode)
  {
    case DebugViewOutputStatisticsMode::Sum:
    default:
      INTERLOCKED_OP_VEC4(InterlockedAddFloat, DebugViewStatistics, value);
      break;
    case DebugViewOutputStatisticsMode::Mean:
      value *= cb.rcpNumOutputPixels;
      INTERLOCKED_OP_VEC4(InterlockedAddFloat, DebugViewStatistics, value);
      break;
 };
#endif
}

void storeToDebugView(uint2 threadId, vec4 value)
{
  vec4 output;

  switch (cb.accumulationMode)
  {
    case DebugViewAccumulationMode::WriteNewOutput:
    default:
      output = value;
      break;
    case DebugViewAccumulationMode::CarryOverPreviousOutput:
      output = PreviousFrameDebugView[threadId];
      break;
    case DebugViewAccumulationMode::BlendNewAndPreviousOutputs:
    {
      const vec4 previousValue = PreviousFrameDebugView[threadId];

      if (cb.enableFp16Accumulation)
      {
        output = lerpFp16(previousValue, value, cb.accumulationWeight);
      }
      else
      {
        output = lerp(previousValue, value, cb.accumulationWeight);
      }
      
      break;
    }
  }

  DebugViewOutput[threadId] = output;
  PreviousFrameDebugView[threadId] = output;

  if (cb.copyOutputToCompositeOutput)
  {
    CompositeOutput[threadId] = output;
  }

  storeDebugViewOutputStatistics(output);
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.debugViewResolution))
  {
    return;
  }

  RNG randomState = createRNG(threadId, cb.frameIdx, 0);
  vec4 outValue = loadInput(randomState, threadId);

  // Post-processing and overrides if applicable
  postprocess(randomState, threadId, outValue);

  storeToDebugView(threadId, outValue);
}
