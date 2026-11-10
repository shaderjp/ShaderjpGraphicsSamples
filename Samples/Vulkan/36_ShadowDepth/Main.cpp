#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int)
{
    Sample sample;
    return tutorial::RunSample(sample,instance,L"Shader.jp | Vulkan | 36 ShadowDepth",{1920,1080});
}
