#pragma once
#include "PathTracingData.h"
#include <numbers>

namespace tutorial
{
// Dedicated GPU layouts; Advanced18 and the legacy RT layouts stay at 80/240 bytes.
struct alignas(16) BSDFMaterial
{
    DirectX::XMFLOAT4 base, factors, emission;
    DirectX::XMUINT4 images;
    DirectX::XMFLOAT4 alpha, dielectric, absorption;
};
struct alignas(16) BSDFConstants
{
    DirectX::XMFLOAT4X4 inverseViewProjection, viewProjection;
    DirectX::XMFLOAT4 camera, sunDirection, sunColor, environment, screen;
    DirectX::XMUINT4 controls, lighting, bsdf;
};
static_assert(sizeof(BSDFMaterial)==112 && offsetof(BSDFMaterial,dielectric)==80 && offsetof(BSDFMaterial,absorption)==96);
static_assert(sizeof(BSDFConstants)==256 && offsetof(BSDFConstants,bsdf)==240);
struct BSDFSceneData
{
    std::vector<RayVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<RayPrimitive> primitives;
    std::vector<BSDFMaterial> materials;
};
inline DirectX::XMFLOAT3 AbsorptionCoefficient(const WorkshopMaterial &m)
{
    auto coefficient=[&](float c) { return m.attenuationDistance==0 ? 0.f : c==0 ? 1e30f : -std::log(c)/m.attenuationDistance; };
    return {coefficient(m.attenuationColor.x),coefficient(m.attenuationColor.y),coefficient(m.attenuationColor.z)};
}
inline BSDFSceneData MakeBSDFSceneData(const Workshop &w)
{
    auto old=MakeRaySceneData(w);
    BSDFSceneData result{std::move(old.vertices),std::move(old.indices),std::move(old.primitives),{}};
    for(size_t i=0;i<w.materials.size();++i) {
        const auto &a=old.materials[i];const auto &m=w.materials[i];const auto sigma=AbsorptionCoefficient(m);
        result.materials.push_back({a.base,a.factors,a.emission,a.images,a.alpha,
            {m.transmission,m.ior,m.thickness,m.attenuationDistance},{sigma.x,sigma.y,sigma.z,0}});
    }
    return result;
}
// Solid angle winding number is independent of duplicated UV/normal vertices.
// Evaluated only when the camera or preset changes. Supports a camera inside a closed volume.
inline uint32_t CameraMedium(const Workshop &w,int scene,DirectX::XMFLOAT3 point)
{
    using namespace DirectX;
    for(const auto &i:w.scenes.at(scene).instances) {
        const auto &p=w.primitives.at(i.primitive);const auto &m=w.materials.at(p.material);
        if(m.transmission==0)continue;
        auto q=XMVector3TransformCoord(XMLoadFloat3(&point),XMMatrixInverse(nullptr,XMLoadFloat4x4(&i.world)));
        DirectX::XMFLOAT3 local;XMStoreFloat3(&local,q);
        if(local.x<p.minimum.x || local.y<p.minimum.y || local.z<p.minimum.z || local.x>p.maximum.x || local.y>p.maximum.y || local.z>p.maximum.z)continue;
        double angle=0;
        for(size_t t=0;t<p.indices.size();t+=3) {
            XMFLOAT3 v[3];
            for(int j=0;j<3;++j)XMStoreFloat3(&v[j],XMLoadFloat3(&p.vertices[p.indices[t+j]].position)-q);
            auto dot=[](XMFLOAT3 a,XMFLOAT3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;};
            auto length=[&](XMFLOAT3 a){return std::sqrt(dot(a,a));};
            XMFLOAT3 cross{v[1].y*v[2].z-v[1].z*v[2].y,v[1].z*v[2].x-v[1].x*v[2].z,v[1].x*v[2].y-v[1].y*v[2].x};
            double a=length(v[0]),b=length(v[1]),c=length(v[2]);
            angle+=2*std::atan2(dot(v[0],cross),a*b*c+dot(v[0],v[1])*c+dot(v[1],v[2])*a+dot(v[2],v[0])*b);
        }
        if(std::abs(angle)>2*std::numbers::pi)return p.material+1;
    }
    return 0;
}
// Small analytical fixtures use the same scene/asset path, not a second renderer.
inline Workshop BSDFFixture(Workshop w,const std::wstring &name)
{
    if(name.empty())return w;
    if(name!=L"dark" && name!=L"furnace" && name!=L"slab" && name!=L"tir" && name!=L"scaled" && name!=L"optics")
        throw std::runtime_error("--bsdf-test: dark / furnace / slab / tir / scaled / optics");
    if(name==L"dark") { for(auto &m:w.materials)m.emission={};w.environmentIntensity=0; }
    if(name==L"furnace" || name==L"slab" || name==L"scaled") {
        for(auto &s:w.scenes) {
            std::erase_if(s.instances,[&](auto &i){return name==L"furnace" ? i.id!="bsdf:instance:ClearSphere" : i.id!="bsdf:instance:ThinPlate" && i.id!="bsdf:instance:ThickPlate";});
            s.lights.clear();s.sun={};
            if(name==L"scaled")for(auto &i:s.instances)if(i.id=="bsdf:instance:ThinPlate") {
                using namespace DirectX;XMStoreFloat4x4(&i.world,XMMatrixScaling(1,1,2)*XMLoadFloat4x4(&i.world));
            }
        }
        if(name!=L"furnace")for(auto &m:w.materials)if(m.transmission>0)m.ior=1;
        w.environmentIntensity=1;
    }
    if(name==L"tir") {
        auto c=std::find_if(w.cameras.begin(),w.cameras.end(),[](auto &c){return c.name=="InsideClear";});
        if(c!=w.cameras.end())std::rotate(w.cameras.begin(),c,c+1);
    }
    return w;
}
} // namespace tutorial
