#include "Sample.h"
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, h, L"Vulkan_Advanced12_BindlessMaterials");
}
