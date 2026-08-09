/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <iostream>

#include "../../../src/dxvk/rtx_render/rtx_texture_upload_generation.h"

int main() {
  dxvk::TextureUploadGeneration generation;

  const uint64_t firstUpload = generation.current();
  if (!generation.isCurrent(firstUpload)) {
    std::cerr << "Initial upload generation was not current\n";
    return -1;
  }

  const uint64_t secondUpload = generation.invalidate();
  if (secondUpload <= firstUpload
   || generation.isCurrent(firstUpload)
   || !generation.isCurrent(secondUpload)) {
    std::cerr << "Invalidation did not reject the stale upload generation\n";
    return -1;
  }

  const uint64_t thirdUpload = generation.invalidate();
  if (thirdUpload <= secondUpload
   || generation.isCurrent(secondUpload)
   || !generation.isCurrent(thirdUpload)) {
    std::cerr << "Repeated invalidation did not remain monotonic\n";
    return -1;
  }

  return 0;
}
