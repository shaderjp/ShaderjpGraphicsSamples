#pragma once
#include "Application.h"
namespace tutorial
{
struct Image
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
Image LoadWicImage(const std::filesystem::path &path);
void SavePng(const std::filesystem::path &path, uint32_t width, uint32_t height, const uint8_t *rgba,
             uint32_t rowPitch);
} // namespace tutorial
