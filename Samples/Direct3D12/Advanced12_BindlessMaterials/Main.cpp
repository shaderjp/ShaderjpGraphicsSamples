#include "Sample.h"
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, h, L"Direct3D12_Advanced12_BindlessMaterials");
}
