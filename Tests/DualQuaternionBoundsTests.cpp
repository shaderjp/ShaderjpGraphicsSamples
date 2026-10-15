#include "../Samples/Shared/DualQuaternionSkinningData.h"
#include <iostream>
using namespace tutorial;
using namespace DirectX;
int main(int argc,char **argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("Repository root required");
        DualQuaternionSkinningData data;
        auto workshop=data.Build(std::filesystem::path(argv[1])/"Assets/Models/SilverRobotAnimated/SilverRobot.gltf");
        auto mesh=BuildMeshletData(workshop);data.PrepareBounds(mesh);
        size_t checked=0,poses=0;
        for(size_t clip=0;clip<data.model.clips.size();++clip)
            for(int frame=0;frame<=4;++frame)
                for(bool dqs:{false,true})
                {
                    data.useDualQuaternion=dqs;
                    auto instances=MakeMeshInstances(workshop,0,0);
                    data.Update(clip,data.model.clips[clip].duration*float(frame)/4,false,0,mesh,instances,true);
                    ++poses;
                    std::span<const SkinDualQuaternion> palette(data.dualQuaternions.data(),data.jointCount);
                    std::vector<XMFLOAT4X4> matrices(data.palettes.begin(),data.palettes.begin()+data.jointCount);
                    for(size_t p=0;p<data.model.primitives.size();++p)
                    {
                        const auto &primitive=mesh.primitives[p];
                        for(uint32_t m=primitive.meshlets.x;m<primitive.meshlets.x+primitive.meshlets.y;++m)
                        {
                            const auto &source=mesh.meshlets[m];auto sphere=data.spheres[m];
                            if(!std::isfinite(sphere.w)||sphere.w<0)throw std::runtime_error("Invalid sphere");
                            for(uint32_t v=0;v<source.offsets.z;++v)
                            {
                                auto vi=primitive.offsets.x+mesh.meshletVertices[source.offsets.x+v];
                                auto vertex=mesh.vertices[vi];auto inf=data.influences[vi];
                                SkinVertex input{vertex.positionU,vertex.normalV,vertex.tangent,inf.joints,inf.weights};
                                auto output=dqs?DeformSkinVertexDqs(input,palette):DeformSkinVertex(input,matrices);
                                float distance=XMVectorGetX(XMVector3Length(XMLoadFloat4(&output.positionU)-XMLoadFloat4(&sphere)));
                                if(distance>sphere.w+3e-5f)throw std::runtime_error("Skinned vertex escaped meshlet sphere");
                                ++checked;
                            }
                        }
                    }
                }
        // The LBS endpoint envelope fails for this twist; the normalization bound must enclose it.
        std::vector<SkinDualQuaternion> palette;
        for(float angle:{XM_PI*4/9,-XM_PI*4/9})
        {
            XMFLOAT4X4 m;XMStoreFloat4x4(&m,XMMatrixRotationZ(angle));palette.push_back(MakeSkinDualQuaternion(m));
        }
        SkinDqsBounds point{{1,0,0},0,{0,1},2};auto sphere=BoundDqsMeshlet(point,palette);
        SkinVertex v{{1,0,0,0},{0,1,0,0},{0,0,1,1},{0,1,0,0},{.5f,.5f,0,0}};
        auto output=DeformSkinVertexDqs(v,palette);
        if(XMVectorGetX(XMVector3Length(XMLoadFloat4(&output.positionU)-XMLoadFloat4(&sphere)))>sphere.w)
            throw std::runtime_error("Twist normalization counterexample failed");
        // Exercise the general N-influence bound with four translated/rotated bones.
        for(int i=2;i<4;++i){XMFLOAT4X4 m;XMStoreFloat4x4(&m,XMMatrixRotationY(float(i))*XMMatrixTranslation(.2f*i,0,0));palette.push_back(MakeSkinDualQuaternion(m));}
        point.joints={0,1,2,3};point.maxInfluences=4;sphere=BoundDqsMeshlet(point,palette);
        for(int i=1;i<=40;++i)
        {
            float a=float(i)/100;v.joints={0,1,2,3};v.weights={a,.2f,.3f,.5f-a};
            output=DeformSkinVertexDqs(v,palette);
            if(XMVectorGetX(XMVector3Length(XMLoadFloat4(&output.positionU)-XMLoadFloat4(&sphere)))>sphere.w)
                throw std::runtime_error("Four-influence sphere failed");
        }
        std::cout<<"DQS bounds PASS: "<<poses<<" poses / "<<checked<<" meshlet vertices; LBS and DQS, twist and four-influence cases\n";
        return 0;
    }
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
