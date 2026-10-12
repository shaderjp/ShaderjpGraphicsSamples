#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace tutorial
{
struct Options
{
    std::filesystem::path model, texture, screenshot, log;
    uint32_t frames = 0;
    bool testWindow = false;
    std::wstring shading; // Sample 07/08 choose their supported display modes.
    bool rotateLight = false;
    bool testUi = false; // Sample 09 exercises actual ImGui widgets through queued input.
    std::filesystem::path scene;
    std::wstring environment;
    bool hideUi = false;
    float occluderOffset = 0;
    std::wstring postView, testImage;
    uint32_t particleCount = 16384;
    bool pauseParticles = false, captureGpu = false, testPost = false, testOdd = false;
    float fixedDelta = 0, gravity = 3, bloomThreshold = 1, bloomStrength = .15f;
    std::wstring workshopScene, preset, camera;
    std::filesystem::path timings;
    bool forward = false, benchmark = false, cameraPath = false;
    bool disableShadows = false, hardShadows = false, disableBloom = false, disableIbl = false, disableDirect = false;
    uint32_t pathFrame = 0;
};

// Only the application lifecycle is shared; graphics resources belong to each sample.
struct ISample
{
    virtual ~ISample() = default;
    virtual void OnInit(HWND window, uint32_t width, uint32_t height, const Options &options) = 0;
    virtual void OnUpdate(float seconds) = 0;
    virtual void OnRender() = 0;
    virtual void OnResize(uint32_t width, uint32_t height) = 0;
    virtual void OnScreenshot(const std::filesystem::path &path) = 0;
    virtual void OnDestroy() noexcept = 0;
    // Optional platform input hook. Graphics and UI backends remain in each sample.
    virtual bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT &) { return false; }
};

int RunSample(ISample &sample, HINSTANCE instance, const wchar_t *title);
void CheckHR(HRESULT result, const char *operation);
void Trace(const std::string &message);
void ValidationError(const std::string &message);
uint32_t ValidationErrorCount();
std::filesystem::path AssetPath(const std::filesystem::path &relative);
std::vector<uint8_t> ReadBinary(const std::filesystem::path &path);
} // namespace tutorial
