#include "Image.h"
#include <limits>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace tutorial
{
namespace
{
ComPtr<IWICImagingFactory> Factory()
{
    ComPtr<IWICImagingFactory> factory;
    CheckHR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "WIC factory");
    return factory;
}
} // namespace
Image LoadWicImage(const std::filesystem::path &path)
{
    try
    {
        auto factory = Factory();
        ComPtr<IWICBitmapDecoder> decoder;
        CheckHR(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnLoad, &decoder),
                "WIC decoder");
        ComPtr<IWICBitmapFrameDecode> frame;
        CheckHR(decoder->GetFrame(0, &frame), "WIC frame");
        Image image;
        CheckHR(frame->GetSize(&image.width, &image.height), "WIC image size");
        const uint64_t bytes = uint64_t(image.width) * image.height * 4;
        if (!image.width || !image.height || bytes > UINT32_MAX)
            throw std::runtime_error("Image is empty or too large");
        ComPtr<IWICFormatConverter> converter;
        CheckHR(factory->CreateFormatConverter(&converter), "WIC converter");
        CheckHR(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                      nullptr, 0, WICBitmapPaletteTypeCustom),
                "WIC RGBA conversion");
        image.rgba.resize(static_cast<size_t>(bytes));
        CheckHR(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(bytes), image.rgba.data()),
                "WIC CopyPixels");
        return image;
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Texture " + path.string() + ": " + error.what());
    }
}
void SavePng(const std::filesystem::path &path, uint32_t width, uint32_t height, const uint8_t *rgba,
             uint32_t rowPitch)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto factory = Factory();
    ComPtr<IWICStream> stream;
    CheckHR(factory->CreateStream(&stream), "WIC stream");
    CheckHR(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "WIC output path");
    ComPtr<IWICBitmapEncoder> encoder;
    CheckHR(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "WIC PNG encoder");
    CheckHR(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "WIC encoder init");
    ComPtr<IWICBitmapFrameEncode> frame;
    CheckHR(encoder->CreateNewFrame(&frame, nullptr), "WIC output frame");
    CheckHR(frame->Initialize(nullptr), "WIC frame init");
    CheckHR(frame->SetSize(width, height), "WIC output size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    CheckHR(frame->SetPixelFormat(&format), "WIC output format");
    // PNG encoders may negotiate BGRA instead of RGBA. Convert to the returned format.
    ComPtr<IWICBitmap> bitmap;
    CheckHR(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA, rowPitch,
                                            rowPitch * height, const_cast<BYTE *>(rgba), &bitmap),
            "WIC source bitmap");
    ComPtr<IWICFormatConverter> converter;
    CheckHR(factory->CreateFormatConverter(&converter), "WIC output converter");
    CheckHR(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom),
            "WIC output conversion");
    CheckHR(frame->WriteSource(converter.Get(), nullptr), "WIC WriteSource");
    CheckHR(frame->Commit(), "WIC frame commit");
    CheckHR(encoder->Commit(), "WIC encoder commit");
}
} // namespace tutorial
