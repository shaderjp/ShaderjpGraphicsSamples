#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Shader.jp | Direct3D12 | 35 DepthPrecision", {1920, 1080});
}
