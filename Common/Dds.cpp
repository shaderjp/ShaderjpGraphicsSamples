#include "DdsImage.h"
#include <DirectXTex.h>
#include <stdexcept>
namespace tutorial
{
DdsImage LoadDds(const std::filesystem::path &path)
{
    try
    {
        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage loaded;
        CheckHR(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, loaded),
                "DDS decode");
        if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.width > 8192 ||
            metadata.height > 8192 || metadata.arraySize != (metadata.IsCubemap() ? 6u : 1u) ||
            !metadata.mipLevels || metadata.mipLevels > 14 ||
            (metadata.format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
             metadata.format != DXGI_FORMAT_R16G16_FLOAT) ||
            (metadata.IsCubemap() && metadata.width != metadata.height))
            throw std::runtime_error("Expected square six-face RGBA16F cube or RG16F/RGBA16F 2D DDS");
        DdsImage result{static_cast<uint32_t>(metadata.width),
                        static_cast<uint32_t>(metadata.height),
                        static_cast<uint32_t>(metadata.arraySize),
                        static_cast<uint32_t>(metadata.mipLevels),
                        metadata.format == DXGI_FORMAT_R16G16_FLOAT ? DdsFormat::Rg16Float
                                                                    : DdsFormat::Rgba16Float,
                        metadata.IsCubemap(),
                        {}};
        for (uint32_t face = 0; face < result.faces; ++face)
            for (uint32_t mip = 0; mip < result.mips; ++mip)
            {
                const auto *image = loaded.GetImage(mip, face, 0);
                if (!image)
                    throw std::runtime_error("Missing DDS subresource");
                DdsSubresource sub{static_cast<uint32_t>(image->width),
                                   static_cast<uint32_t>(image->height),
                                   face,
                                   mip,
                                   image->rowPitch,
                                   image->slicePitch,
                                   {}};
                sub.bytes.assign(image->pixels, image->pixels + image->slicePitch);
                // IEEE half exponent 31 means Inf/NaN; all lighting data must be finite.
                for (size_t i = 0; i + 1 < sub.bytes.size(); i += 2)
                    if ((((uint16_t(sub.bytes[i + 1]) << 8) | sub.bytes[i]) & 0x7c00) == 0x7c00)
                        throw std::runtime_error("DDS contains non-finite half values");
                result.subresources.push_back(std::move(sub));
            }
        return result;
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("DDS " + path.string() + ": " + error.what());
    }
}
} // namespace tutorial
