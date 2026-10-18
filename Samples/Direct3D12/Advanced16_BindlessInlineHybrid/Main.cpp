#include "Sample.h"
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, h, L"Shader.jp | Direct3D12 | Advanced16_BindlessInlineHybrid");
}
