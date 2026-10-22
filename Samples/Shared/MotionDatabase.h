#pragma once
#include "SkinnedGltf.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numeric>

namespace tutorial
{
namespace motion
{
using namespace DirectX;
inline XMFLOAT3 Add(XMFLOAT3 a, XMFLOAT3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline XMFLOAT3 Sub(XMFLOAT3 a, XMFLOAT3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline XMFLOAT3 Mul(XMFLOAT3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(XMFLOAT3 a,XMFLOAT3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(XMFLOAT3 a) { return std::sqrt(Dot(a,a)); }
inline XMFLOAT3 Unit(XMFLOAT3 a) { return Mul(a,1/std::max(Length(a),1e-7f)); }
inline XMFLOAT3 Rotate(XMFLOAT3 p,float yaw) { return {p.x*std::cos(yaw)+p.z*std::sin(yaw),p.y,-p.x*std::sin(yaw)+p.z*std::cos(yaw)}; }
inline float Angle(float a) { return std::remainder(a,XM_2PI); }
inline XMFLOAT3 Point(const SkinPose &p,size_t n) { const auto &m=p.globals.at(n);return {m._41,m._42,m._43}; }
inline uint64_t HashBytes(const std::vector<uint8_t> &bytes) { uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h; }
inline size_t Node(const GltfSkinnedModel &model,const std::string &name)
{
    auto i=std::find_if(model.nodes.begin(),model.nodes.end(),[&](const auto &n){return n.name==name;});
    if(i==model.nodes.end())throw std::runtime_error("Motion set missing bone: "+name);
    return size_t(i-model.nodes.begin());
}
inline constexpr size_t Features=30;
using Feature=std::array<float,Features>;
struct Root { XMFLOAT3 position{};float yaw=0; };
struct Clip
{
    size_t modelClip=0;
    std::string name;
    bool loop=false,locomotion=false,spatial=false;
    float duration=0;
    std::vector<Root> roots;
    std::vector<std::array<bool,2>> contacts;
    Root At(float time) const
    {
        const int cycles=loop?int(std::floor(time/duration)):0;
        const float t=loop?time-float(cycles)*duration:std::clamp(time,0.f,duration);
        const float frame=t/duration*float(roots.size()-1);
        const size_t a=std::min(size_t(frame),roots.size()-1),b=std::min(a+1,roots.size()-1);
        const float f=frame-float(a);
        Root r{Add(roots[a].position,Mul(Sub(roots[b].position,roots[a].position),f)), roots[a].yaw+Angle(roots[b].yaw-roots[a].yaw)*f};
        if(cycles){r.position=Add(r.position,Mul(roots.back().position,float(cycles)));r.yaw+=float(cycles)*roots.back().yaw;}
        return r;
    }
    std::array<bool,2> Contact(float time) const
    {
        const float t=loop?time-std::floor(time/duration)*duration:std::clamp(time,0.f,duration);
        return contacts.at(std::min(size_t(t/duration*float(contacts.size()-1)),contacts.size()-1));
    }
};
struct Frame { uint32_t clip=0;float time=0;Feature values{}; };
struct Database
{
    std::vector<Clip> clips;
    std::vector<Frame> frames;
    Feature mean{},scale{};
    uint64_t fingerprint=0;
    std::array<size_t,2> feet{},hips{};
    void LoadMetadata(const GltfSkinnedModel &model,const std::filesystem::path &directory)
    {
        const auto bytes=ReadBinary(directory/"MotionSet.json");
        const auto doc=nlohmann::json::parse(bytes);
        if(doc.at("version")!=1 || doc.at("fps")!=30)throw std::runtime_error("Unsupported motion metadata");
        fingerprint=HashBytes(bytes)^HashBytes(ReadBinary(directory/"SilverRobot.gltf"))^(HashBytes(ReadBinary(directory/"SilverRobot.bin"))<<1);
        feet={Node(model,"Foot.L"),Node(model,"Foot.R")};hips={Node(model,"Hips"),Node(model,"Root")};
        clips.clear();frames.clear();
        for(const auto &j:doc.at("clips"))
        {
            Clip c;c.name=j.at("name");c.loop=j.at("loop");c.locomotion=j.at("kind")=="locomotion";c.spatial=j.at("spatial");c.duration=j.at("duration");
            auto it=std::find_if(model.clips.begin(),model.clips.end(),[&](const auto &a){return a.name==c.name;});
            if(it==model.clips.end() || c.duration<=0 || std::abs(it->duration-c.duration)>.002f)throw std::runtime_error("Motion clip mismatch: "+c.name);
            c.modelClip=size_t(it-model.clips.begin());
            for(const auto &r:j.at("root")){Root v{{r.at(0),r.at(1),r.at(2)},r.at(3)};if(!std::isfinite(v.yaw+v.position.x+v.position.y+v.position.z))throw std::runtime_error("Non-finite root");c.roots.push_back(v);}
            for(const auto &r:j.at("contacts"))c.contacts.push_back({r.at(0),r.at(1)});
            if(c.roots.size()<2 || c.contacts.size()!=c.roots.size())throw std::runtime_error("Invalid motion sample count");
            clips.push_back(std::move(c));
        }
    }
    size_t Find(const std::string &name) const
    {
        auto it=std::find_if(clips.begin(),clips.end(),[&](const auto &c){return c.name==name;});
        if(it==clips.end())throw std::runtime_error("Missing motion: "+name);
        return size_t(it-clips.begin());
    }
    SkinLocalPose Pose(const GltfSkinnedModel &m,size_t clip,float time) const
    {
        const auto &c=clips.at(clip);
        return EvaluateGltfLocalPose(m,c.modelClip,c.loop?time-std::floor(time/c.duration)*c.duration:std::clamp(time,0.f,c.duration));
    }
    Feature Extract(const GltfSkinnedModel &model,size_t clip,float t) const
    {
        const auto &c=clips[clip];const auto root=c.At(t);const float d=1.f/30;
        const float t0=c.loop?t-d:std::max(0.f,t-d),t1=c.loop?t+d:std::min(c.duration,t+d);
        const auto p=MakeGltfGlobalPose(Pose(model,clip,t)),a=MakeGltfGlobalPose(Pose(model,clip,t0)),b=MakeGltfGlobalPose(Pose(model,clip,t1));
        Feature x{};size_t k=0;
        auto put=[&](XMFLOAT3 v){x[k++]=v.x;x[k++]=v.y;x[k++]=v.z;};
        for(auto n:feet)put(Point(p,n));
        for(auto n:feet)put(Mul(Sub(Point(b,n),Point(a,n)),1/(t1-t0)));
        put(Mul(Sub(Point(b,hips[0]),Point(a,hips[0])),1/(t1-t0)));
        for(float horizon:{.2f,.4f,.6f})
        {
            auto future=c.At(t+horizon);auto delta=Rotate(Sub(future.position,root.position),-root.yaw);
            x[k++]=delta.x;x[k++]=delta.z;x[k++]=-std::sin(future.yaw-root.yaw);x[k++]=-std::cos(future.yaw-root.yaw);
        }
        put(Rotate(Mul(Sub(c.At(t1).position,c.At(t0).position),1/(t1-t0)),-root.yaw));
        return x;
    }
    void Build(const GltfSkinnedModel &model)
    {
        frames.clear();mean.fill(0);scale.fill(0);
        for(size_t c=0;c<clips.size();++c)if(clips[c].locomotion)
            for(size_t f=0;f+1<clips[c].roots.size();++f)
            {
                const float t=float(f)/30;
                if(!clips[c].loop && t+.6f>clips[c].duration)continue;
                frames.push_back({uint32_t(c),t,Extract(model,c,t)});
            }
        if(frames.empty())throw std::runtime_error("Empty matching database");
        for(const auto &f:frames)for(size_t j=0;j<Features;++j)mean[j]+=f.values[j]/float(frames.size());
        for(const auto &f:frames)for(size_t j=0;j<Features;++j){float d=f.values[j]-mean[j];scale[j]+=d*d/float(frames.size());}
        for(auto &s:scale)s=1/std::max(std::sqrt(s),.1f);
        for(auto &f:frames)for(size_t j=0;j<Features;++j)f.values[j]=(f.values[j]-mean[j])*scale[j];
    }
    void Save(const std::filesystem::path &path) const
    {
        std::ofstream out(path,std::ios::binary);uint32_t version=1,count=uint32_t(frames.size()),features=uint32_t(Features);
        auto write=[&](const auto &v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));};
        write(version);write(features);write(fingerprint);write(count);write(mean);write(scale);
        for(const auto &f:frames){write(f.clip);write(f.time);write(f.values);}
        if(!out)throw std::runtime_error("Cannot save matching database");
    }
    void Load(const std::filesystem::path &path)
    {
        std::ifstream in(path,std::ios::binary);uint32_t version=0,features=0,count=0;uint64_t hash=0;
        auto read=[&](auto &v){in.read(reinterpret_cast<char*>(&v),sizeof(v));};
        read(version);read(features);read(hash);read(count);
        if(!in || version!=1 || features!=Features || hash!=fingerprint || count==0 || count>1000000)throw std::runtime_error("Stale/invalid motion database; rebuild with MotionDatabaseBuilder");
        read(mean);read(scale);frames.resize(count);
        for(auto &f:frames){read(f.clip);read(f.time);read(f.values);if(f.clip>=clips.size() || !std::isfinite(f.time) || f.time<0 || f.time>=clips[f.clip].duration || !clips[f.clip].locomotion)throw std::runtime_error("Invalid database frame");for(float x:f.values)if(!std::isfinite(x))throw std::runtime_error("Non-finite database feature");}
        for(size_t j=0;j<Features;++j)if(!std::isfinite(mean[j]) || !std::isfinite(scale[j]) || scale[j]<=0)throw std::runtime_error("Invalid database normalization");
        if(!in || in.peek()!=EOF)throw std::runtime_error("Truncated/oversized motion database");
    }
    Feature Normalize(Feature x) const {for(size_t i=0;i<Features;++i)x[i]=(x[i]-mean[i])*scale[i];return x;}
    static float Weight(size_t i,const std::array<float,3> &w)
    {
        return i<6?w[0]/6:i<15?w[0]*.3f/9:i<27?w[1]/12:w[2]/3;
    }
    float Cost(const Feature &query,const Feature &candidate,const std::array<float,3> &weights) const
    {
        float cost=0;for(size_t j=0;j<Features;++j){float d=query[j]-candidate[j];cost+=d*d*Weight(j,weights);}return cost;
    }
    size_t Search(const Feature &normalized,const std::array<float,3> &weights) const
    {
        size_t best=0;float value=FLT_MAX;
        for(size_t i=0;i<frames.size();++i){const float cost=Cost(normalized,frames[i].values,weights);if(cost<value){best=i;value=cost;}}
        return best;
    }
};
} // namespace motion
} // namespace tutorial
