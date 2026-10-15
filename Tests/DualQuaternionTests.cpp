#include "DualQuaternion.h"
#include "../Samples/Shared/DualQuaternionSkinningData.h"
#include <iostream>
#include <limits>

using namespace tutorial;
using namespace DirectX;
void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void Near(float a, float b, float tolerance = 3e-5f) { Require(std::abs(a-b) < tolerance, "DQS numeric mismatch"); }
template<class F> void Reject(F f) { bool failed=false; try { f(); } catch(const std::runtime_error &) { failed=true; } Require(failed,"Invalid DQS input accepted"); }
XMFLOAT4X4 Store(FXMMATRIX m) { XMFLOAT4X4 f; XMStoreFloat4x4(&f,m); return f; }
int main(int argc, char **argv)
{
    try
    {
        Require(argc==2,"Repository root required");
        SkinVertex vertex{{1,0,0,.3f},{0,0,1,.7f},{0,1,0,-1},{0,1,0,0},{.5f,.5f,0,0}};
        constexpr float twist = XM_PI * 4 / 9; // 160-degree separation avoids the ambiguous exact half turn.
        std::vector<XMFLOAT4X4> matrices={Store(XMMatrixRotationZ(twist)),Store(XMMatrixRotationZ(-twist))};
        std::vector<SkinDualQuaternion> palette;
        for (auto &m : matrices) palette.push_back(MakeSkinDualQuaternion(m));
        auto lbs=DeformSkinVertex(vertex,matrices), dqs=DeformSkinVertexDqs(vertex,palette);
        Near(lbs.positionU.x,std::cos(twist)); Near(dqs.positionU.x,1); Near(dqs.positionU.y,0);
        Near(dqs.positionU.w,.3f); Near(dqs.normalV.w,.7f); Near(dqs.tangent.w,-1);
        // Antipodal representations must flip real AND dual, including translation.
        matrices[0]=Store(XMMatrixRotationY(.8f)*XMMatrixTranslation(2,3,-4));
        palette[0]=MakeSkinDualQuaternion(matrices[0]); palette[1]=palette[0];
        for(int k=0;k<4;++k){(&palette[1].real.x)[k]*=-1;(&palette[1].dual.x)[k]*=-1;}
        matrices[1]=matrices[0];
        dqs=DeformSkinVertexDqs(vertex,palette);lbs=DeformSkinVertex(vertex,matrices);
        for(int k=0;k<3;++k) Near((&dqs.positionU.x)[k],(&lbs.positionU.x)[k]);
        auto blend=BlendSkinDualQuaternions(vertex,palette);
        Near(DqDot(blend.real,blend.real),1);Near(DqDot(blend.real,blend.dual),0);
        // Exercise all four slots with an analytically known weighted translation.
        std::vector<SkinDualQuaternion> four;
        for(int i=0;i<4;++i) four.push_back(MakeSkinDualQuaternion(Store(XMMatrixTranslation(float(i),0,0))));
        auto fourVertex=vertex; fourVertex.joints={0,1,2,3};fourVertex.weights={.1f,.2f,.3f,.4f};
        Near(DeformSkinVertexDqs(fourVertex,four).positionU.x,3);
        Reject([&]{MakeSkinDualQuaternion(Store(XMMatrixScaling(1,2,1)));});
        Reject([&]{MakeSkinDualQuaternion(Store(XMMatrixScaling(-1,1,1)));});
        auto bad=Store(XMMatrixIdentity());bad._12=.1f;Reject([&]{MakeSkinDualQuaternion(bad);});
        bad._12=std::numeric_limits<float>::quiet_NaN();Reject([&]{MakeSkinDualQuaternion(bad);});
        auto invalid=vertex;invalid.weights={0,0,0,0};Reject([&]{DeformSkinVertexDqs(invalid,palette);});
        invalid=vertex;invalid.joints.x=99;Reject([&]{DeformSkinVertexDqs(invalid,palette);});
        invalid=vertex;invalid.weights.x=-1;Reject([&]{DeformSkinVertexDqs(invalid,palette);});
        // Audit every original clip and runtime twist, at 30 fps, using the actual asset.
        DualQuaternionSkinningData data;
        data.Build(std::filesystem::path(argv[1])/"Assets/Models/SilverRobotAnimated/SilverRobot.gltf");
        size_t poses=0,single=0;float maximumSingleError=0,maximumBlendDelta=0;
        for(size_t clip=0;clip<data.model.clips.size();++clip)
            for(int frame=0;frame<=int(std::ceil(data.model.clips[clip].duration*30));++frame)
            {
                auto pose=EvaluateGltfPose(data.model,clip,float(frame)/30);
                auto poseMatrices=MakeSkinPalette(data.model,pose,0);
                std::vector<SkinDualQuaternion> joints;
                for(auto &m:poseMatrices)joints.push_back(MakeSkinDualQuaternion(m));
                ++poses;
                // Sample geometry across every material; all palette entries are audited above.
                for(auto &primitive:data.model.primitives)
                    for(size_t vi=0;vi<primitive.vertices.size();vi+=31)
                    {
                        auto &v=primitive.vertices[vi];auto a=DeformSkinVertex(v,poseMatrices),b=DeformSkinVertexDqs(v,joints);
                        float delta=XMVectorGetX(XMVector3Length(XMLoadFloat4(&a.positionU)-XMLoadFloat4(&b.positionU)));
                        Require(std::isfinite(delta),"Nonfinite DQS result");
                        maximumBlendDelta=std::max(maximumBlendDelta,delta);
                        if(v.weights.x==1){++single;maximumSingleError=std::max(maximumSingleError,delta);}
                        Near(XMVectorGetX(XMVector3Length(XMLoadFloat4(&b.normalV))),1);
                        Near(XMVectorGetX(XMVector3Dot(XMLoadFloat4(&b.normalV),XMLoadFloat4(&b.tangent))),0);
                    }
            }
        Require(maximumSingleError<1e-5f,"Single-bone DQS differs from LBS");
        std::cout<<"DQS CPU PASS: "<<poses<<" poses / "<<single<<" single-bone vertices; max single error "
                 <<maximumSingleError<<" m; max blend difference "<<maximumBlendDelta<<" m\n";
        return 0;
    }
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
