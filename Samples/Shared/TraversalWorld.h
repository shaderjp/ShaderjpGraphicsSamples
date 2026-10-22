#pragma once
#include "MotionDatabase.h"
#include <set>
namespace tutorial::motion
{
struct Obstacle
{
    std::string id;
    XMFLOAT3 center{},half{};
    float yaw=0;
    float Top() const{return center.y+half.y;}
};
struct Hit { bool hit=false;float time=1;XMFLOAT3 normal{};size_t obstacle=0; };
struct World
{
    std::vector<Obstacle> boxes;
    XMFLOAT3 spawn{0,0,1};
    std::vector<XMFLOAT3> checkpoints;
    struct Target {std::string obstacle,id,role;XMFLOAT3 position;};
    std::vector<Target> targets;
    float radius=.25f,height=1.8f;
    void Load(const std::filesystem::path &path)
    {
        const auto d=nlohmann::json::parse(ReadBinary(path));if(d.at("version")!=1)throw std::runtime_error("Unknown traversal level version");
        auto vec=[](const auto &v){XMFLOAT3 p{v.at(0),v.at(1),v.at(2)};if(!std::isfinite(p.x+p.y+p.z))throw std::runtime_error("Non-finite level vector");return p;};
        boxes.clear();checkpoints.clear();targets.clear();std::set<std::string> ids;
        for(const auto &j:d.at("boxes"))
        {
            Obstacle b;b.id=j.at("id");b.center=vec(j.at("center"));b.half=vec(j.at("half"));b.yaw=j.at("yaw");
            if(!ids.insert(b.id).second || std::min({b.half.x,b.half.y,b.half.z})<=0 || !std::isfinite(b.yaw))throw std::runtime_error("Invalid level box");boxes.push_back(b);
        }
        for(const auto &j:d.at("targets"))
        {
            Target t{j.at("obstacle"),j.at("id"),j.at("role"),vec(j.at("position"))};
            if(!t.obstacle.empty() && !ids.contains(t.obstacle))throw std::runtime_error("Unknown target obstacle");
            if(t.role=="spawn")spawn=t.position;else if(t.role=="checkpoint")checkpoints.push_back(t.position);else throw std::runtime_error("Unsupported traversal marker role");targets.push_back(t);
        }
    }
    // Exact distance from a vertical capsule's axis segment to an upright box.
    // Conservative advancement uses the distance as a safe lower bound on TOI.
    float Separation(XMFLOAT3 feet,const Obstacle &box,float capsuleHeight,float offset,XMFLOAT3 &normal) const
    {
        const auto p=Rotate(Sub(feet,box.center),-box.yaw);
        // Compute segment length first: for a sphere, differently rounded sums
        // can otherwise produce high < low and violate std::clamp's precondition.
        const float low=p.y+offset+radius,high=low+std::max(0.f,capsuleHeight-2*radius);
        XMFLOAT3 axis{p.x,std::clamp(0.f,low,high),p.z};
        const XMFLOAT3 closest{std::clamp(axis.x,-box.half.x,box.half.x),std::clamp(axis.y,-box.half.y,box.half.y),std::clamp(axis.z,-box.half.z,box.half.z)};
        const auto delta=Sub(axis,closest);const float distance=Length(delta);
        if(distance>1e-6f){normal=Rotate(Mul(delta,1/distance),box.yaw);return distance-radius;}
        const std::array<float,6> depths={p.x+box.half.x+radius,box.half.x-p.x+radius,p.y+offset+capsuleHeight+box.half.y,box.half.y-p.y-offset,p.z+box.half.z+radius,box.half.z-p.z+radius};
        const size_t side=size_t(std::min_element(depths.begin(),depths.end())-depths.begin());
        const std::array<XMFLOAT3,6> normals={XMFLOAT3{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
        normal=Rotate(normals[side],box.yaw);return -depths[side];
    }
    Hit Sweep(XMFLOAT3 from,XMFLOAT3 delta,float capsuleHeight=1.8f,float offset=0) const
    {
        Hit best;const float length=Length(delta);if(length<1e-8f)return best;
        for(size_t i=0;i<boxes.size();++i)
        {
            float t=0;
            for(int iteration=0;iteration<64 && t<=best.time;++iteration)
            {
                XMFLOAT3 normal;const float distance=Separation(Add(from,Mul(delta,t)),boxes[i],capsuleHeight,offset,normal);
                if(distance<.0005f)
                {
                    if(Dot(delta,normal)<-1e-7f || distance<-.001f){best={true,t,normal,i};}
                    break;
                }
                t+=std::max(distance-.0002f,.00005f)/length;
            }
        }
        return best;
    }
    bool Clear(XMFLOAT3 p,float capsuleHeight=1.8f,float offset=0) const
    {
        for(const auto &box:boxes){XMFLOAT3 n;if(Separation(p,box,capsuleHeight,offset,n)<-.001f)return false;}return true;
    }
    XMFLOAT3 Move(XMFLOAT3 from,XMFLOAT3 delta,float capsuleHeight=1.8f,float offset=0) const
    {
        auto result=from;
        for(int i=0;i<5 && Length(delta)>1e-7f;++i)
        {
            const auto hit=Sweep(result,delta,capsuleHeight,offset);
            if(!hit.hit){result=Add(result,delta);break;}
            const float t=std::max(0.f,hit.time-.0001f);result=Add(result,Mul(delta,t));delta=Mul(delta,1-t);
            delta=Sub(delta,Mul(hit.normal,std::min(0.f,Dot(delta,hit.normal))));
        }
        return result;
    }
    float Penetration(XMFLOAT3 p,float capsuleHeight=1.8f,float offset=0) const
    {
        float worst=0;for(const auto &b:boxes){XMFLOAT3 n;worst=std::max(worst,-Separation(p,b,capsuleHeight,offset,n));}return worst;
    }
};
} // namespace tutorial::motion
