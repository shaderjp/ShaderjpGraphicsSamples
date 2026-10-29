#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Direct3D12_Advanced20_ClusteredGeometry", {1920, 1080});
}
