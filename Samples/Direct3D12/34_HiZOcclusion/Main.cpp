#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Shader.jp | Direct3D12 | 34 HiZOcclusion", {1920, 1080});
}
