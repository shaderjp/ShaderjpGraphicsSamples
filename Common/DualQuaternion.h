#pragma once
#include "SkinnedGltf.h"
#include <cmath>
#include <span>
#include <stdexcept>

namespace tutorial
{
// xyzw Hamilton quaternions. A rigid transform stores q + epsilon * (t*q)/2.
struct alignas(16) SkinDualQuaternion
{
    DirectX::XMFLOAT4 real{0, 0, 0, 1}, dual{};
};
static_assert(sizeof(SkinDualQuaternion) == 32);
inline DirectX::XMFLOAT4 DqMultiply(const DirectX::XMFLOAT4 &a, const DirectX::XMFLOAT4 &b)
{
    return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}
inline float DqDot(const DirectX::XMFLOAT4 &a, const DirectX::XMFLOAT4 &b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}
inline SkinDualQuaternion MakeSkinDualQuaternion(const DirectX::XMFLOAT4X4 &matrix)
{
    using namespace DirectX;
    for (float value : std::span(&matrix.m[0][0], 16))
        if (!std::isfinite(value))
            throw std::runtime_error("DQS: non-finite joint transform");
    // Export roundoff is accepted; scale, reflection and shear require another method.
    constexpr float tolerance = 1e-4f;
    if (std::abs(matrix._14) > tolerance || std::abs(matrix._24) > tolerance ||
        std::abs(matrix._34) > tolerance || std::abs(matrix._44 - 1) > tolerance)
        throw std::runtime_error("DQS: joint transform is not affine");
    auto m = XMLoadFloat4x4(&matrix);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::abs(XMVectorGetX(XMVector3Dot(m.r[i], m.r[j])) - (i == j ? 1.f : 0.f)) > tolerance)
                throw std::runtime_error("DQS: scale/shear is unsupported; rigid transforms required");
    if (std::abs(XMVectorGetX(XMMatrixDeterminant(m)) - 1) > tolerance)
        throw std::runtime_error("DQS: reflection is unsupported; rigid transforms required");
    SkinDualQuaternion result;
    XMStoreFloat4(&result.real, XMQuaternionNormalize(XMQuaternionRotationMatrix(m)));
    result.dual = DqMultiply({matrix._41, matrix._42, matrix._43, 0}, result.real);
    for (int k = 0; k < 4; ++k)
        (&result.dual.x)[k] *= .5f;
    return result;
}
inline SkinDualQuaternion BlendSkinDualQuaternions(const SkinVertex &v,
                                                   std::span<const SkinDualQuaternion> palette)
{
    int reference = 0;
    float total = 0;
    for (int k = 0; k < 4; ++k)
    {
        float w = (&v.weights.x)[k];
        if (!std::isfinite(w) || w < 0 || (&v.joints.x)[k] >= palette.size())
            throw std::runtime_error("DQS: invalid joint/weight");
        total += w;
        if (w > (&v.weights.x)[reference]) reference = k;
    }
    if (!(total > 1e-8f)) throw std::runtime_error("DQS: zero weights");
    auto anchor = palette[(&v.joints.x)[reference]].real;
    SkinDualQuaternion result{{}, {}};
    for (int k = 0; k < 4; ++k)
    {
        const auto &q = palette[(&v.joints.x)[k]];
        float weight = (&v.weights.x)[k] * (DqDot(anchor, q.real) < 0 ? -1.f : 1.f);
        for (int component = 0; component < 4; ++component)
        {
            (&result.real.x)[component] += weight * (&q.real.x)[component];
            (&result.dual.x)[component] += weight * (&q.dual.x)[component];
        }
    }
    float length = std::sqrt(DqDot(result.real, result.real));
    if (!(length > 1e-8f)) throw std::runtime_error("DQS: degenerate blend");
    for (int k = 0; k < 4; ++k)
    {
        (&result.real.x)[k] /= length;
        (&result.dual.x)[k] /= length;
    }
    float projection = DqDot(result.real, result.dual);
    for (int k = 0; k < 4; ++k)
        (&result.dual.x)[k] -= projection * (&result.real.x)[k];
    return result;
}
inline SkinDeformedVertex DeformSkinVertexDqs(const SkinVertex &v,
                                             std::span<const SkinDualQuaternion> palette)
{
    using namespace DirectX;
    auto q = BlendSkinDualQuaternions(v, palette);
    XMFLOAT4 conjugate{-q.real.x, -q.real.y, -q.real.z, q.real.w};
    auto rotate = [&](XMFLOAT4 p) { p.w = 0; return DqMultiply(DqMultiply(q.real, p), conjugate); };
    SkinDeformedVertex out{v.positionU, v.normalV, v.tangent};
    auto p = rotate(v.positionU), t = DqMultiply(q.dual, conjugate);
    out.positionU = {p.x + 2*t.x, p.y + 2*t.y, p.z + 2*t.z, v.positionU.w};
    auto n = rotate(v.normalV), tangent = rotate(v.tangent);
    auto normal = XMVector3Normalize(XMLoadFloat4(&n));
    auto direction = XMLoadFloat4(&tangent);
    direction = XMVector3Normalize(direction - normal * XMVector3Dot(normal, direction));
    XMFLOAT3 normal3, tangent3;
    XMStoreFloat3(&normal3, normal); XMStoreFloat3(&tangent3, direction);
    out.normalV = {normal3.x,normal3.y,normal3.z,v.normalV.w};
    out.tangent = {tangent3.x,tangent3.y,tangent3.z,v.tangent.w};
    return out;
}
} // namespace tutorial
