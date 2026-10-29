#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Direct3D12_Advanced21_ClusterTemplates", {1920, 1080});
}
