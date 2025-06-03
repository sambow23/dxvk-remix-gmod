// ACES operator by Stephen Hill, MJP, David Neubelt
// The file is received from: https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// SWIPAT filed under: https://nvbugspro.nvidia.com/bug/5182738

//=================================================================================================
//
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

// The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v, bool suppressBlackLevelClamp)
{
    // NOTE: ACES with 'suppressBlackLevelClamp=true' is only used in the local tonemapper
    //       to calculate a multiplier for a given pixel to evaluate its local intensity
    //       to apply per-pixel exposure normalization to preserve more details, and
    //       only after that, the actual ACES is applied over that normalized color.
    //       Without 'suppressBlackLevelClamp=true', the local tonemapper will
    //       incorrectly crash the almost-black colors because of the local intensity evaluation.
    //       Look the comments in 'final_combine.comp.slang'

    float3 a = v * (v + 0.0245786f) - (suppressBlackLevelClamp ? 0.f : 0.000090537f);
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color, bool suppressBlackLevelClamp)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color, suppressBlackLevelClamp);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

// AgX tone mapping implementation
// Based on the reference implementation with proper log-space handling

float3 AgXToneMapping(float3 color, float gamma, float saturation, float exposureOffset) {
    // AgX constants from the reference implementation
    const float AgX_min_ev = -12.47393;
    const float AgX_max_ev = 4.026069;
    
    // Apply exposure offset
    color = color * pow(2.0, exposureOffset);
    
    // Convert to log2 space, avoiding log(0)
    float3 logColor = log2(max(color, 1e-10));
    
    // Normalize to [0, 1] range
    logColor = (logColor - AgX_min_ev) / (AgX_max_ev - AgX_min_ev);
    logColor = saturate(logColor);
    
    // Apply the AgX curve with improved contrast
    float3 x = logColor;
    float3 x2 = x * x;
    float3 x3 = x2 * x;
    float3 x4 = x2 * x2;
    float3 x5 = x4 * x;
    float3 x6 = x3 * x3;
    
    // Enhanced AgX curve with better contrast
    float3 result = -0.0023 + 0.1191 * x + 0.4298 * x2 - 6.868 * x3 + 31.96 * x4 - 40.14 * x5 + 15.5 * x6;
    
    // Apply gamma adjustment for contrast control
    result = pow(saturate(result), gamma);
    
    // Apply saturation adjustment
    float luma = dot(result, float3(0.2126, 0.7152, 0.0722));
    result = lerp(float3(luma, luma, luma), result, saturation);
    
    return saturate(result);
}
