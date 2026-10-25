#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Vulkan_Advanced19_BSDFMaterials", {1920, 1080, 16});
}
