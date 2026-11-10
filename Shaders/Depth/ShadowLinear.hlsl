#include "Depth.hlsli"
float PSMain(Output input):SV_Target0
{
    // A color attachment, not SV_Depth. Hardware projection depth still selects the visible face.
    return (input.viewZ-c.range.x)/(c.range.y-c.range.x);
}
