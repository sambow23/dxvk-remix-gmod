#pragma once

#include <cstdint>
#include <string>

namespace dxvk::fork_hooks {

  bool writeRgba8PngFile(const std::string& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba);

} // namespace dxvk::fork_hooks
