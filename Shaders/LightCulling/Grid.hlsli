// b0/t0/s0 keep their existing bindings. The grid starts at b1 / binding 19.
VK_BINDING(19,0) cbuffer LightGrid : register(b1)
{
    row_major float4x4 worldToView;
    float4 projectionInfo; // tan(FOVx/2), tan(FOVy/2), projection A, projection B
    float4 depthRange;     // near, far, padding, unused
    uint4 gridSize;        // tile count X/Y, slice count, physical list stride
    uint4 gridControls;    // 0 all / 1 tiled / 2 clustered, view, list limit, unused
};
float ViewDepth(float depth) { return projectionInfo.w / (depth - projectionInfo.z); }
uint DepthSlice(float z)
{
    return min(uint(max(0.0, floor(log2(max(z,depthRange.x)/depthRange.x)
                 * gridSize.z / log2(depthRange.y/depthRange.x)))), gridSize.z-1);
}
uint CellIndex(uint2 pixel,float depth)
{
    uint2 tile=pixel/16;
    uint z=gridControls.x==2 ? DepthSlice(ViewDepth(depth)) : 0;
    return (z*gridSize.y+tile.y)*gridSize.x+tile.x;
}
#ifdef BUILD_LIGHT_LISTS
VK_BINDING(20,0) RWStructuredBuffer<uint2> lightHeaders:register(u1);
VK_BINDING(21,0) RWStructuredBuffer<uint> lightIndices:register(u2);
VK_BINDING(22,0) RWStructuredBuffer<float2> tileDepthBounds:register(u3);
VK_BINDING(23,0) RWStructuredBuffer<uint> tileSliceMasks:register(u4);
#else
VK_BINDING(20,0) StructuredBuffer<uint2> lightHeaders:register(t15);
VK_BINDING(21,0) StructuredBuffer<uint> lightIndices:register(t16);
#endif
