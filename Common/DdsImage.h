#pragma once
#include "Application.h"
namespace tutorial
{
enum class DdsFormat
{
    Rgba16Float,
    Rg16Float
};
struct DdsSubresource
{
    uint32_t width = 0, height = 0, face = 0, mip = 0;
    size_t rowPitch = 0, slicePitch = 0;
    std::vector<uint8_t> bytes;
};
struct DdsImage
{
    uint32_t width = 0, height = 0, faces = 0, mips = 0;
    DdsFormat format{};
    bool cube = false;
    std::vector<DdsSubresource> subresources; // face-major, then mip.
};
DdsImage LoadDds(const std::filesystem::path &path);
} // namespace tutorial
