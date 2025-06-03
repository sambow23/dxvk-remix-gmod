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

// AgX tone mapping implementation based on https://github.com/sobotka/AgX
// Implementation follows Troy Sobotka's AgX 
float3 AgXLog2(float3 x) {
    return log2(max(x, float3(1e-10, 1e-10, 1e-10)));
}

float3 AgXPow2(float3 x) {
    return pow(2.0, x);
}

float3 AgXLook(float3 x) {
    const float3 a = float3(0.856627153315983, 0.856627153315983, 0.856627153315983);
    const float3 b = float3(0.047996709891013, 0.047996709891013, 0.047996709891013);
    const float3 c = float3(0.895906626106349, 0.895906626106349, 0.895906626106349);
    const float3 d = float3(0.266839800085409, 0.266839800085409, 0.266839800085409);
    const float3 e = float3(0.358254953079327, 0.358254953079327, 0.358254953079327);
    const float3 f = float3(0.137692104802882, 0.137692104802882, 0.137692104802882);
    
    return (a * x + b) / (c * x + d) + e * x + f;
}

float3 AgXToneMapping(float3 color) {
    // AgX constants
    const float AgXMinEv = -12.47393;
    const float AgXMaxEv = 4.026069;
    
    // Convert to log2 space
    float3 logColor = AgXLog2(color);
    
    // Apply AgX scale and offset in log space
    logColor = (logColor - AgXMinEv) / (AgXMaxEv - AgXMinEv);
    
    // Apply the AgX look transform
    logColor = AgXLook(logColor);
    
    // Clamp to [0, 1]
    return saturate(logColor);
}
