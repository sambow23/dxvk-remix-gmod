#include "rtx_fork_png.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <vector>

namespace dxvk::fork_hooks {
  namespace {
    constexpr std::array<std::uint8_t, 8> kPngSignature = { 137, 80, 78, 71, 13, 10, 26, 10 };

    void appendU32Be(std::vector<std::uint8_t>& data, std::uint32_t value) {
      data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
      data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
      data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
      data.push_back(static_cast<std::uint8_t>(value & 0xff));
    }

    std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
      std::uint32_t crc = 0xffffffffu;
      for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
      }
      return crc ^ 0xffffffffu;
    }

    std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
      constexpr std::uint32_t kMod = 65521u;
      std::uint32_t a = 1u;
      std::uint32_t b = 0u;
      for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
      }
      return (b << 16) | a;
    }

    void appendChunk(std::vector<std::uint8_t>& png, const char type[4], const std::vector<std::uint8_t>& payload) {
      appendU32Be(png, static_cast<std::uint32_t>(payload.size()));
      const std::size_t typeOffset = png.size();
      png.insert(png.end(), type, type + 4);
      png.insert(png.end(), payload.begin(), payload.end());
      appendU32Be(png, crc32(png.data() + typeOffset, 4 + payload.size()));
    }

    bool appendImageRows(std::vector<std::uint8_t>& rows, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba) {
      const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
      if (width == 0 || height == 0 || rgba == nullptr || rowBytes / 4u != width) {
        return false;
      }

      const std::size_t rowWithFilter = rowBytes + 1u;
      if (rowWithFilter < rowBytes || height > std::numeric_limits<std::size_t>::max() / rowWithFilter) {
        return false;
      }

      rows.reserve(static_cast<std::size_t>(height) * rowWithFilter);
      for (std::uint32_t y = 0; y < height; ++y) {
        rows.push_back(0);
        const std::uint8_t* row = rgba + (static_cast<std::size_t>(y) * rowBytes);
        rows.insert(rows.end(), row, row + rowBytes);
      }
      return true;
    }

    std::vector<std::uint8_t> makeUncompressedZlibStream(const std::vector<std::uint8_t>& data) {
      std::vector<std::uint8_t> stream;
      stream.reserve(data.size() + (data.size() / 65535u + 1u) * 5u + 6u);
      stream.push_back(0x78);
      stream.push_back(0x01);

      std::size_t offset = 0;
      do {
        const std::size_t remaining = data.size() - offset;
        const std::uint16_t blockSize = static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535u));
        const bool finalBlock = offset + blockSize == data.size();
        stream.push_back(finalBlock ? 0x01 : 0x00);
        stream.push_back(static_cast<std::uint8_t>(blockSize & 0xff));
        stream.push_back(static_cast<std::uint8_t>((blockSize >> 8) & 0xff));
        const std::uint16_t inverseSize = static_cast<std::uint16_t>(~blockSize);
        stream.push_back(static_cast<std::uint8_t>(inverseSize & 0xff));
        stream.push_back(static_cast<std::uint8_t>((inverseSize >> 8) & 0xff));
        stream.insert(stream.end(), data.begin() + offset, data.begin() + offset + blockSize);
        offset += blockSize;
      } while (offset < data.size());

      appendU32Be(stream, adler32(data.data(), data.size()));
      return stream;
    }
  }

  bool writeRgba8PngFile(const std::string& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba) {
    std::vector<std::uint8_t> rows;
    if (!appendImageRows(rows, width, height, rgba)) {
      return false;
    }

    std::vector<std::uint8_t> png;
    png.insert(png.end(), kPngSignature.begin(), kPngSignature.end());

    std::vector<std::uint8_t> ihdr;
    appendU32Be(ihdr, width);
    appendU32Be(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", makeUncompressedZlibStream(rows));
    appendChunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }

    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
  }

} // namespace dxvk::fork_hooks
