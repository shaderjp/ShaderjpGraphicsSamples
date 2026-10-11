#include "Application.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <objbase.h>
#include <shellapi.h>
#include <sstream>
#include <stdexcept>

namespace tutorial
{
namespace
{
std::mutex logMutex;
std::ofstream logFile;
uint32_t validationErrors = 0;
struct WindowState
{
    uint32_t width = 800, height = 600;
    bool resized = false, minimized = false;
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *state = reinterpret_cast<WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        state = static_cast<WindowState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_SIZE && state)
    {
        state->minimized = wParam == SIZE_MINIMIZED;
        state->width = LOWORD(lParam);
        state->height = HIWORD(lParam);
        state->resized = true;
        return 0;
    }
    if (message == WM_TIMER)
    {
        KillTimer(window, 1);
        ShowWindow(window, SW_RESTORE);
        RECT rectangle{0, 0, 800, 600};
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(window, nullptr, 0, 0, rectangle.right - rectangle.left,
                     rectangle.bottom - rectangle.top, SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
Options ParseOptions()
{
    Options options;
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        throw std::runtime_error("CommandLineToArgvW failed");
    try
    {
        for (int i = 1; i < count; ++i)
        {
            std::wstring argument = arguments[i];
            if (argument == L"--test-window")
            {
                options.testWindow = true;
                continue;
            }
            if (i + 1 >= count)
                throw std::runtime_error("Missing command-line option value");
            std::wstring value = arguments[++i];
            if (argument == L"--model")
                options.model = std::filesystem::absolute(value);
            else if (argument == L"--texture")
                options.texture = std::filesystem::absolute(value);
            else if (argument == L"--screenshot")
                options.screenshot = std::filesystem::absolute(value);
            else if (argument == L"--log")
                options.log = std::filesystem::absolute(value);
            else if (argument == L"--frames")
            {
                size_t consumed = 0;
                const auto frames = std::stoull(value, &consumed);
                if (value.empty() || value.front() == L'-' || consumed != value.size() || frames == 0 ||
                    frames > UINT32_MAX)
                    throw std::runtime_error("--frames must be a positive 32-bit integer");
                options.frames = static_cast<uint32_t>(frames);
            }
            else
                throw std::runtime_error("Unknown command-line option");
        }
    }
    catch (...)
    {
        LocalFree(arguments);
        throw;
    }
    LocalFree(arguments);
    return options;
}
} // namespace
void Trace(const std::string &message)
{
    std::lock_guard lock(logMutex);
    OutputDebugStringA((message + "\n").c_str());
    std::fprintf(stdout, "%s\n", message.c_str());
    std::fflush(stdout);
    if (logFile)
    {
        logFile << message << '\n';
        logFile.flush();
    }
}
void ValidationError(const std::string &message)
{
    {
        std::lock_guard lock(logMutex);
        ++validationErrors;
    }
    Trace("VALIDATION ERROR: " + message);
}
uint32_t ValidationErrorCount()
{
    std::lock_guard lock(logMutex);
    return validationErrors;
}
void CheckHR(HRESULT result, const char *operation)
{
    if (FAILED(result))
    {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<uint32_t>(result) << ')';
        throw std::runtime_error(message.str());
    }
}
std::filesystem::path AssetPath(const std::filesystem::path &relative)
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot resolve executable directory");
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path() / relative;
}
std::vector<uint8_t> ReadBinary(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open file: " + path.string());
    const auto size = stream.tellg();
    if (size <= 0)
        throw std::runtime_error("File is empty or unreadable: " + path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read file: " + path.string());
    return bytes;
}
int RunSample(ISample &sample, HINSTANCE instance, const wchar_t *title)
{
    Options options;
    HWND window = nullptr;
    bool comInitialized = false;
    WindowState state;
    int exitCode = 0;
    try
    {
        options = ParseOptions();
        if (!options.log.empty())
        {
            if (!options.log.parent_path().empty())
                std::filesystem::create_directories(options.log.parent_path());
            logFile.open(options.log);
            if (!logFile)
                throw std::runtime_error("Cannot create log file");
        }
        CheckHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
        comInitialized = true;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = L"ShaderjpTutorial";
        if (!RegisterClassExW(&windowClass))
            throw std::runtime_error("RegisterClassExW failed");
        RECT rectangle{0, 0, 800, 600};
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        window = CreateWindowExW(0, windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, rectangle.right - rectangle.left,
                                 rectangle.bottom - rectangle.top, nullptr, nullptr, instance, &state);
        if (!window)
            throw std::runtime_error("CreateWindowExW failed");
        sample.OnInit(window, state.width, state.height, options);
        state.resized = false;
        ShowWindow(window, SW_SHOW);
        Trace("Initialized (800 x 600)");
        const auto start = std::chrono::steady_clock::now();
        uint32_t frames = 0;
        bool captured = false, running = true;
        while (running)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running)
                break;
            if (state.minimized || !state.width || !state.height)
            {
                WaitMessage();
                continue;
            }
            if (state.resized)
            {
                sample.OnResize(state.width, state.height);
                state.resized = false;
                Trace("Resized: " + std::to_string(state.width) + " x " + std::to_string(state.height));
            }
            sample.OnUpdate(std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count());
            sample.OnRender();
            ++frames;
            if (options.testWindow && frames == 12)
            {
                RECT resized{0, 0, 640, 480};
                AdjustWindowRect(&resized, WS_OVERLAPPEDWINDOW, FALSE);
                SetWindowPos(window, nullptr, 0, 0, resized.right - resized.left,
                             resized.bottom - resized.top, SWP_NOMOVE | SWP_NOZORDER);
            }
            if (options.testWindow && frames == 24)
            {
                Trace("Minimize/restore test");
                SetTimer(window, 1, 100, nullptr);
                ShowWindow(window, SW_MINIMIZE);
            }
            const bool finished = options.frames && frames >= options.frames;
            if (!captured && !options.screenshot.empty() && (finished || (!options.frames && frames == 8)))
            {
                sample.OnScreenshot(options.screenshot);
                captured = true;
                Trace("Screenshot: " + options.screenshot.string());
            }
            if (finished)
                running = false;
        }
        Trace("Rendered frames: " + std::to_string(frames));
    }
    catch (const std::exception &error)
    {
        Trace(std::string("ERROR: ") + error.what());
        exitCode = 1;
        if (!options.frames)
            MessageBoxA(window, error.what(), "Shader.jp sample error", MB_OK | MB_ICONERROR);
    }
    sample.OnDestroy();
    if (window && IsWindow(window))
        DestroyWindow(window);
    if (comInitialized)
        CoUninitialize();
    if (ValidationErrorCount())
        exitCode = 2;
    Trace("Validation errors: " + std::to_string(ValidationErrorCount()));
    logFile.close();
    return exitCode;
}
} // namespace tutorial
