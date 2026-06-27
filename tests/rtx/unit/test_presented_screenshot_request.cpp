#include "../../../src/dxvk/rtx_render/rtx_fork_screenshot.h"
#include "../../../src/dxvk/rtx_render/rtx_fork_png.h"

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main() {
  dxvk::fork_hooks::PresentedScreenshotQueue queue;

  assert(queue.request("C:/tmp/first.png"));
  assert(!queue.request("C:/tmp/second.png"));

  auto first = queue.consume();
  assert(first.has_value());
  assert(*first == "C:/tmp/first.png");
  assert(!queue.consume().has_value());

  assert(!queue.request(""));
  assert(!queue.request(nullptr));
  assert(queue.request("C:/tmp/third.png"));
  queue.clear();
  assert(!queue.consume().has_value());

  const std::filesystem::path pngPath = std::filesystem::temp_directory_path() / "mcrtx-presented-screenshot-test.png";
  const std::vector<std::uint8_t> pixels = {
    255, 0, 0, 255,
    0, 255, 0, 255,
  };
  assert(dxvk::fork_hooks::writeRgba8PngFile(pngPath.string(), 2, 1, pixels.data()));

  std::ifstream pngFile(pngPath, std::ios::binary);
  std::uint8_t signature[8] = {};
  pngFile.read(reinterpret_cast<char*>(signature), sizeof(signature));
  const std::uint8_t expectedSignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  assert(pngFile.gcount() == sizeof(signature));
  assert(std::equal(std::begin(signature), std::end(signature), std::begin(expectedSignature)));
  std::filesystem::remove(pngPath);

  std::cout << "presented screenshot request tests passed\n";
  return 0;
}
