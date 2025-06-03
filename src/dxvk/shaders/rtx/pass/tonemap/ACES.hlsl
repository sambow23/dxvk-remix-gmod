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

// AgX tone mapping implementation based on https://github.com/MrLixm/AgXc
// Enhanced implementation with proper color space transforms and multiple looks

// AgX Base Transform (Rec.2020 primaries to AgX working space)
static const float3x3 AgX_InputTransform = 
{
    {0.842479062253094, 0.0784335999999992,  0.0792237451477643},
    {0.0423282422610123, 0.878468636469772,  0.0791661274605434},
    {0.0423756549057051, 0.0784336,          0.879142973793104}
};

// AgX Base Transform Output (AgX working space to Rec.2020 primaries)
static const float3x3 AgX_OutputTransform =
{
    { 1.19687900512017, -0.0980208811401368, -0.0990297440797205},
    {-0.0528968517574562, 1.15190312639708,   -0.0989611768448433},
    {-0.0529716355144438, -0.0980434501171241, 1.15107367264116}
};

// AgX curve implementation - more accurate polynomial
float agx_default_contrast_approx(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    float x6 = x4 * x2;
    
    return + 15.5 * x6
           - 40.14 * x4 * x
           + 31.96 * x4
           - 6.868 * x2 * x
           + 0.4298 * x2
           + 0.1191 * x
           - 0.00232;
}

// AgX Look transforms
float3 agx_look_punchy(float3 val) {
    const float3x3 punchy = {
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0}, 
        {0.0, 0.0, 1.0}
    };
    
    // Increase contrast and saturation
    val = pow(val, 0.7);
    val = lerp(dot(val, float3(0.2126, 0.7152, 0.0722)), val, 1.4);
    return val;
}

float3 agx_look_golden(float3 val) {
    // Golden hour look - warm highlights, cooler shadows
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    float3 warm = val * float3(1.1, 1.05, 0.9);
    float3 cool = val * float3(0.9, 0.95, 1.1);
    return lerp(cool, warm, smoothstep(0.2, 0.8, luma));
}

float3 agx_look_greyscale(float3 val) {
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    return float3(luma, luma, luma);
}

float3 apply_agx_look(float3 val, int look) {
    switch(look) {
        case 1: return agx_look_punchy(val);
        case 2: return agx_look_golden(val);
        case 3: return agx_look_greyscale(val);
        default: return val;
    }
}

float3 AgXToneMapping(float3 color, float gamma, float saturation, float exposureOffset, 
                      int look, float contrast, float slope, float power) {
    // Apply exposure offset
    color = color * pow(2.0, exposureOffset);
    
    // Input transform to AgX working space
    color = mul(AgX_InputTransform, color);
    
    // Ensure positive values for log transform
    color = max(color, 1e-10);
    
    // Convert to log2 space
    color = log2(color);
    
    // AgX constants
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    
    // Normalize to [0, 1] range
    color = (color - min_ev) / (max_ev - min_ev);
    color = saturate(color);
    
    // Apply contrast and slope adjustments
    color = pow(color, 1.0 / contrast);
    color = color * slope;
    
    // Apply the AgX curve per channel
    color.r = agx_default_contrast_approx(color.r);
    color.g = agx_default_contrast_approx(color.g);
    color.b = agx_default_contrast_approx(color.b);
    
    // Apply power adjustment for midtone response
    color = pow(saturate(color), power);
    
    // Output transform back to display space
    color = mul(AgX_OutputTransform, color);
    
    // Apply look
    color = apply_agx_look(color, look);
    
    // Apply gamma correction
    color = pow(saturate(color), 1.0 / gamma);
    
    // Apply saturation adjustment
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, saturation);
    
    return saturate(color);
}
