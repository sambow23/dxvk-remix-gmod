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

#include "ui_bindings.h"

layout(push_constant)
ConstantBuffer<UiPushConstants> pc;

[shader("vertex")]
float4 main(in float3 inPos, in float2 UV, in float4 color, out float2 oUV : TEXCOORD, out float4 oColor : COLOR) : SV_Position
{
  oUV = UV;
  oColor = color;

  // inPos.xy are top-left-origin pixels mapped to NDC; inPos.z is normalized
  // depth [0,1] (0 for flat 2D quads, perspective-divided depth for 3D).
  return float4(inPos.xy * pc.scale + pc.translate, inPos.z, 1.f);
}
