#include "Sample.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Sample sample;
    return tutorial::RunSample(sample, instance, L"Direct3D12_Advanced23_PartitionedTLAS", {1920, 1080});
}
