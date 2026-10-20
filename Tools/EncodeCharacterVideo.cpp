// Offline GPU-frame recording tool. Uses only Windows SDK WIC and Media Foundation.
#include "Image.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
using Microsoft::WRL::ComPtr;
using tutorial::CheckHR;
namespace
{
constexpr UINT32 Width = 1280, Height = 720, Fps = 60, Count = 300;
constexpr LONGLONG Clock = 10000000;
BYTE Byte(float value) { return BYTE(std::clamp(std::lround(value), 0l, 255l)); }
void Encode(const std::filesystem::path &directory, const std::filesystem::path &output)
{
    ComPtr<IMFAttributes> attributes;
    CheckHR(MFCreateAttributes(&attributes, 1), "Video attributes");
    CheckHR(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE), "Video throttling");
    ComPtr<IMFSinkWriter> writer;
    CheckHR(MFCreateSinkWriterFromURL(output.c_str(), nullptr, attributes.Get(), &writer), "MP4 sink writer");
    ComPtr<IMFMediaType> encoded, input;
    CheckHR(MFCreateMediaType(&encoded), "Encoded media type");
    CheckHR(encoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Video type");
    CheckHR(encoded->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "H.264 codec");
    CheckHR(encoded->SetUINT32(MF_MT_AVG_BITRATE, 6000000), "Video bitrate");
    CheckHR(encoded->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Progressive video");
    CheckHR(MFSetAttributeSize(encoded.Get(), MF_MT_FRAME_SIZE, Width, Height), "Video size");
    CheckHR(MFSetAttributeRatio(encoded.Get(), MF_MT_FRAME_RATE, Fps, 1), "Video frame rate");
    CheckHR(MFSetAttributeRatio(encoded.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Pixel aspect ratio");
    CheckHR(encoded->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709), "BT.709 matrix");
    CheckHR(encoded->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235), "Video range");
    DWORD stream = 0;
    CheckHR(writer->AddStream(encoded.Get(), &stream), "Video stream");
    CheckHR(MFCreateMediaType(&input), "Input media type");
    CheckHR(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Input video");
    CheckHR(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "NV12 input");
    CheckHR(input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Input progressive");
    CheckHR(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, Width, Height), "Input size");
    CheckHR(MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, Fps, 1), "Input frame rate");
    CheckHR(MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Input pixel aspect");
    CheckHR(input->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709), "Input matrix");
    CheckHR(input->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235), "Input range");
    CheckHR(writer->SetInputMediaType(stream, input.Get(), nullptr), "Video input format");
    CheckHR(writer->BeginWriting(), "Begin MP4");
    for (UINT32 frame = 0; frame < Count; ++frame)
    {
        std::wostringstream name;
        name << std::setfill(L'0') << std::setw(6) << frame + 1 << L".png";
        const auto image = tutorial::LoadWicImage(directory / name.str());
        if (image.width != Width || image.height != Height)
            throw std::runtime_error("Video frame must be 1280x720");
        ComPtr<IMFMediaBuffer> buffer;
        CheckHR(MFCreateMemoryBuffer(Width * Height * 3 / 2, &buffer), "Video frame buffer");
        BYTE *pixels = nullptr;
        CheckHR(buffer->Lock(&pixels, nullptr, nullptr), "Video buffer lock");
        // Display-referred RGB -> limited-range BT.709 NV12, with 2x2 chroma averaging.
        for (UINT32 y = 0; y < Height; ++y)
            for (UINT32 x = 0; x < Width; ++x)
            {
                const auto *p = &image.rgba[(y * Width + x) * 4];
                pixels[y * Width + x] = Byte(16 + .182586f * p[0] + .614231f * p[1] + .062007f * p[2]);
            }
        for (UINT32 y = 0; y < Height; y += 2)
            for (UINT32 x = 0; x < Width; x += 2)
            {
                float r = 0, g = 0, b = 0;
                for (UINT32 j = 0; j < 2; ++j)
                    for (UINT32 i = 0; i < 2; ++i)
                    {
                        const auto *p = &image.rgba[((y + j) * Width + x + i) * 4];
                        r += p[0] * .25f; g += p[1] * .25f; b += p[2] * .25f;
                    }
                const UINT32 uv = Width * Height + (y / 2) * Width + x;
                pixels[uv] = Byte(128 - .100644f * r - .338572f * g + .439216f * b);
                pixels[uv + 1] = Byte(128 + .439216f * r - .398942f * g - .040274f * b);
            }
        CheckHR(buffer->Unlock(), "Video buffer unlock");
        CheckHR(buffer->SetCurrentLength(Width * Height * 3 / 2), "Video frame length");
        ComPtr<IMFSample> sample;
        CheckHR(MFCreateSample(&sample), "Video sample");
        CheckHR(sample->AddBuffer(buffer.Get()), "Video sample buffer");
        const LONGLONG time = LONGLONG(frame) * Clock / Fps;
        CheckHR(sample->SetSampleTime(time), "Video timestamp");
        CheckHR(sample->SetSampleDuration(LONGLONG(frame + 1) * Clock / Fps - time), "Video duration");
        CheckHR(writer->WriteSample(stream, sample.Get()), "Encode GPU frame");
    }
    CheckHR(writer->Finalize(), "Finalize MP4");
}
}
int wmain(int argc, wchar_t **argv)
{
    bool com = false, media = false;
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: EncodeCharacterVideo <PNG directory> <output.mp4>");
        CheckHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Video COM"); com = true;
        CheckHR(MFStartup(MF_VERSION), "Media Foundation startup"); media = true;
        Encode(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        CheckHR(MFShutdown(), "Media Foundation shutdown"); media = false;
        CoUninitialize(); com = false;
        std::cout << "Encoded 300 real GPU frames: H.264, 1280x720, 60 fps, 5 seconds\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        if (media) MFShutdown();
        if (com) CoUninitialize();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
