#pragma once
#include "DualQuaternion.h"
#include <cfloat>
#include <vector>
namespace tutorial
{
struct SkinDqsBounds
{
    DirectX::XMFLOAT3 center{};
    float radius = 0;
    std::vector<uint32_t> joints;
    uint32_t maxInfluences = 1;
};
// For rest-space p=c+v and chosen output origin C, |v|<=r. Each bone's
// translated center is bi=Ri*c+ti. Normalized DQ blending gives
// |p'-C| <= r + max_i|bi-C| / lowerBound(|sum wi*qi|/sum wi).
// One influence: 1; two after hemisphere correction: 1/sqrt(2).
// N>=3: projection onto the largest-weight reference is >=1/N.
inline DirectX::XMFLOAT4 BoundDqsMeshlet(const SkinDqsBounds &source,
                                        std::span<const SkinDualQuaternion> palette)
{
    using namespace DirectX;
    auto transform = [&](uint32_t joint)
    {
        const auto &q = palette[joint];
        XMFLOAT4 conjugate{-q.real.x,-q.real.y,-q.real.z,q.real.w};
        auto translation = DqMultiply(q.dual,conjugate);
        auto c = XMVector3Rotate(XMLoadFloat3(&source.center),XMLoadFloat4(&q.real));
        return c + XMVectorSet(2*translation.x,2*translation.y,2*translation.z,0);
    };
    auto lo=XMVectorReplicate(FLT_MAX), hi=XMVectorReplicate(-FLT_MAX);
    for (auto joint:source.joints)
    {
        auto p=transform(joint); lo=XMVectorMin(lo,p); hi=XMVectorMax(hi,p);
    }
    auto center=(lo+hi)*.5f;
    float displacement=0;
    for (auto joint:source.joints)
        displacement=std::max(displacement,XMVectorGetX(XMVector3Length(transform(joint)-center)));
    float factor=source.maxInfluences==2 ? std::sqrt(2.f) : float(source.maxInfluences);
    XMFLOAT4 sphere; XMStoreFloat4(&sphere,center);
    sphere.w=source.radius+factor*displacement;
    // Covers float CPU/GPU arithmetic and source sphere rounding, in mesh-local metres.
    sphere.w+=.001f+5e-5f*sphere.w;
    return sphere;
}
} // namespace tutorial
