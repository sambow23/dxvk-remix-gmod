/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/pass/post_fx/rcas.h"
#include "rtx/pass/post_fx/rcas.slangh"

layout(binding = RCAS_INPUT)
Sampler2D InputTexture;

layout(binding = RCAS_OUTPUT, rgba16f)
RWTexture2D<float4> OutputTexture;

layout(push_constant)
ConstantBuffer<RcasArgs> cb;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID) {
  // Bounds check
  if (any(threadId >= cb.imageSize)) {
    return;
  }
  
  float2 texCoord = (float2(threadId) + 0.5) / float2(cb.imageSize);
  
  // Sample center and 4 neighbors in cross pattern
  //    b
  //  d e f
  //    h
  float3 e = InputTexture.SampleLevel(texCoord, 0).rgb;
  float3 b = InputTexture.SampleLevel(texCoord, 0, int2(0, -1)).rgb;
  float3 d = InputTexture.SampleLevel(texCoord, 0, int2(-1, 0)).rgb;
  float3 f = InputTexture.SampleLevel(texCoord, 0, int2(1, 0)).rgb;
  float3 h = InputTexture.SampleLevel(texCoord, 0, int2(0, 1)).rgb;
  
  // Apply RCAS sharpening
  float3 output = applyRCAS(e, b, d, f, h, cb.sharpness);
  
  // Write result
  OutputTexture[threadId] = float4(output, 1.0);
}
