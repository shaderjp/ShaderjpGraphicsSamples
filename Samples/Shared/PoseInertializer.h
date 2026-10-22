#pragma once
#include "MotionDatabase.h"
namespace tutorial::motion
{
inline XMFLOAT3 QuatLog(XMVECTOR q)
{
    if(XMVectorGetW(q)<0)q=XMVectorNegate(q);
    XMFLOAT4 v;XMStoreFloat4(&v,XMQuaternionNormalize(q));
    const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    const float factor=length>1e-6f?2*std::atan2(length,v.w)/length:2.f;
    return {v.x*factor,v.y*factor,v.z*factor};
}
inline XMVECTOR QuatExp(XMFLOAT3 v)
{
    const float length=Length(v);
    return length>1e-6f?XMQuaternionRotationAxis(XMLoadFloat3(&v),length):XMQuaternionIdentity();
}
inline void Decay(XMFLOAT3 &x,XMFLOAT3 &v,float dt,float halfLife)
{
    const float omega=1.67834699f/std::max(halfLife,.001f),e=std::exp(-omega*dt);
    const auto j=Add(v,Mul(x,omega));
    x=Mul(Add(x,Mul(j,dt)),e);v=Mul(Sub(v,Mul(j,omega*dt)),e);
}
struct Inertializer
{
    struct Offset { XMFLOAT3 translation{},velocity{},rotation{},angular{}; };
    std::vector<Offset> offsets;
    void Reset(size_t n) { offsets.assign(n,{}); }
    void Transition(const SkinLocalPose &displayed,const SkinLocalPose &previous,const SkinLocalPose &target,const SkinLocalPose &next,float dt)
    {
        Reset(target.nodes.size());
        for(size_t i=0;i<offsets.size();++i)
        {
            auto &o=offsets[i];const auto &a=displayed.nodes[i],&p=previous.nodes[i],&b=target.nodes[i],&n=next.nodes[i];
            o.translation=Sub(a.translation,b.translation);
            o.velocity=Mul(Sub(Sub(a.translation,p.translation),Sub(n.translation,b.translation)),1/dt);
            o.rotation=QuatLog(XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&b.rotation)),XMLoadFloat4(&a.rotation)));
            const auto va=QuatLog(XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&p.rotation)),XMLoadFloat4(&a.rotation)));
            const auto vb=QuatLog(XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&b.rotation)),XMLoadFloat4(&n.rotation)));
            o.angular=Mul(Sub(va,vb),1/dt);
        }
    }
    SkinLocalPose Apply(SkinLocalPose pose,float dt,bool enabled)
    {
        for(size_t i=0;i<offsets.size();++i)
        {
            auto &o=offsets[i];Decay(o.translation,o.velocity,dt,.08f);Decay(o.rotation,o.angular,dt,.08f);
            if(!enabled)continue;
            pose.nodes[i].translation=Add(pose.nodes[i].translation,o.translation);
            XMStoreFloat4(&pose.nodes[i].rotation,XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&pose.nodes[i].rotation),QuatExp(o.rotation))));
        }
        return pose;
    }
};
inline XMVECTOR FromTo(XMFLOAT3 from,XMFLOAT3 to)
{
    const auto a=XMVector3Normalize(XMLoadFloat3(&from)),b=XMVector3Normalize(XMLoadFloat3(&to));
    const float dot=XMVectorGetX(XMVector3Dot(a,b));
    if(dot<-.9999f){const auto axis=XMVector3Cross(a,std::abs(XMVectorGetX(a))<.8f?XMVectorSet(1,0,0,0):XMVectorSet(0,0,1,0));return XMQuaternionRotationAxis(axis,XM_PI);}
    return XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(a,b),1+dot));
}
inline void RotateBone(SkinLocalPose &pose,size_t bone,XMFLOAT3 from,XMFLOAT3 to)
{
    const auto globals=MakeGltfGlobalPose(pose);auto world=XMLoadFloat4x4(&globals.globals[bone]);
    world.r[3]=XMVectorSet(0,0,0,1);world*=XMMatrixRotationQuaternion(FromTo(from,to));
    const auto &original=globals.globals[bone];world.r[3]=XMVectorSet(original._41,original._42,original._43,1);
    const int parent=pose.nodes[bone].parent;
    if(parent>=0)world*=XMMatrixInverse(nullptr,XMLoadFloat4x4(&globals.globals[size_t(parent)]));
    XMVECTOR s,q,t;XMMatrixDecompose(&s,&q,&t,world);XMStoreFloat4(&pose.nodes[bone].rotation,XMQuaternionNormalize(q));
}
inline float SolveLimb(SkinLocalPose &pose,size_t upper,size_t lower,size_t end,XMFLOAT3 target)
{
    auto globals=MakeGltfGlobalPose(pose);const auto a=Point(globals,upper),b=Point(globals,lower),c=Point(globals,end);
    const float l1=Length(Sub(b,a)),l2=Length(Sub(c,b));
    auto dir=Sub(target,a);const float reach=std::clamp(Length(dir),std::abs(l1-l2)+.0001f,l1+l2-.0001f);dir=Unit(dir);
    auto pole=Sub(Sub(b,a),Mul(dir,Dot(Sub(b,a),dir)));
    if(Length(pole)<1e-5f)pole={0,0,-1};pole=Unit(pole);
    const float along=(l1*l1+reach*reach-l2*l2)/(2*reach),height=std::sqrt(std::max(0.f,l1*l1-along*along));
    const auto knee=Add(a,Add(Mul(dir,along),Mul(pole,height)));
    RotateBone(pose,upper,Sub(b,a),Sub(knee,a));
    globals=MakeGltfGlobalPose(pose);const auto joint=Point(globals,lower),tip=Point(globals,end);
    RotateBone(pose,lower,Sub(tip,joint),Sub(target,joint));
    return Length(Sub(Point(MakeGltfGlobalPose(pose),end),target));
}
} // namespace tutorial::motion
