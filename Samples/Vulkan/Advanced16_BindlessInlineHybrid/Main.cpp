#include "Sample.h"
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, h, L"Shader.jp | Vulkan | Advanced16_BindlessInlineHybrid");
}
