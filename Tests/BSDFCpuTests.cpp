#include "../Samples/Shared/BSDFData.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace tutorial;
void Require(bool v,const char *why) {if(!v)throw std::runtime_error(why);}
int main(int argc,char **argv) {
    try {
        const std::filesystem::path root=argc>1?argv[1]:".";
        auto w=LoadBSDFScene(root/"Assets/Scenes/BSDFMaterials/BSDFMaterials.json");
        Require(w.scenes.size()==2,"two composed presets");
        auto gpu=MakeBSDFSceneData(w);Require(gpu.materials.size()==w.materials.size(),"material conversion");
        uint32_t volumes=0;
        for(const auto &m:w.materials)if(m.transmission>0){++volumes;Require(m.ior==1.5f && m.thickness>0,"glass defaults");}
        Require(volumes==3,"three dielectric materials");
        Require(CameraMedium(w,0,{0,2.55f,-7.6f})==0,"outside starts in air");
        Require(CameraMedium(w,0,{-1.25f,.65f,-.95f})>0,"inside closed sphere");
        Require(CameraMedium(w,0,{0,1,0})==0,"between objects is air");
        auto &scene=w.scenes[0];
        for(const auto &i:scene.instances)Require(i.id!="instance:WhiteBox" && i.id!="instance:SmoothSphere" && i.id!="instance:RoughSphere","old props excluded by stable ID");
        bool rejected=false;try{LoadPathTracingScene(root/"Assets/Scenes/BSDFMaterials/BSDFMaterials.json");}catch(const std::exception&){rejected=true;}
        Require(rejected,"legacy path profile remains strict");
        WorkshopMaterial m;m.attenuationColor={.25f,.5f,.75f};m.attenuationDistance=1;
        auto a=AbsorptionCoefficient(m);Require(std::abs(std::exp(-a.x*2)-.0625f)<1e-6,"distance absorption");
        m.attenuationDistance=0;a=AbsorptionCoefficient(m);Require(a.x==0 && a.y==0 && a.z==0,"no absorption default");
        const auto fixtures=root/".cache/bsdf19/cpu-invalid";
        std::filesystem::create_directories(fixtures);
        std::filesystem::copy(root/"Assets/Models/BSDFMaterials",fixtures/"Model",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        using Json=nlohmann::json;
        auto read=[](const std::filesystem::path &p){std::ifstream f(p);return Json::parse(f);};
        auto config=read(root/"Assets/Scenes/BSDFMaterials/BSDFMaterials.json");
        config["model"]="Model/BSDFMaterials.gltf";config["baseScene"]=(root/"Assets/Scenes/PathTracing/PathTracing.json").generic_string();
        config["environment"]["prefix"]=(root/"Assets/Environments/Workshop/WorkshopSky").generic_string();
        std::ofstream(fixtures/"scene.json")<<config.dump(2);
        const auto model=read(fixtures/"Model/BSDFMaterials.gltf");
        size_t glass=0;while(!model["materials"][glass].contains("extensions"))++glass;
        for(int test=0;test<9;++test){
            auto bad=model;auto &mat=bad["materials"][glass];auto &ext=mat["extensions"];
            if(test==0)ext["KHR_materials_transmission"]["transmissionFactor"]=.5;
            if(test==1)ext["KHR_materials_ior"]["ior"]=.8;
            if(test==2)ext["KHR_materials_volume"]["thicknessFactor"]=0;
            if(test==3)ext["KHR_materials_volume"]["attenuationDistance"]=0;
            if(test==4)mat["alphaMode"]="BLEND";
            if(test==5)ext["KHR_materials_volume"]["thicknessTexture"]={{"index",0}};
            if(test==6){bad["extensionsUsed"].push_back("KHR_materials_clearcoat");}
            if(test==8)ext["KHR_materials_transmission"]["transmissionTexture"]={{"index",0}};
            if(test==7){
                for(auto &mesh:bad["meshes"])for(auto &primitive:mesh["primitives"])if(primitive["material"]==glass){
                    auto index=primitive["indices"].get<size_t>();bad["accessors"][index]["count"]=bad["accessors"][index]["count"].get<size_t>()-3;goto opened;
                }
                opened:;
            }
            std::ofstream(fixtures/"Model/BSDFMaterials.gltf")<<bad.dump(2);
            rejected=false;try{LoadBSDFScene(fixtures/"scene.json");}catch(const std::exception&){rejected=true;}
            Require(rejected,"unsupported material or open volume must fail");
        }
        std::ofstream(fixtures/"Model/BSDFMaterials.gltf")<<model.dump(2);
        auto valid=LoadBSDFScene(fixtures/"scene.json");Require(valid.scenes.size()==2,"valid profile after failure cases");
        auto scaled=model;
        for(auto &node:scaled["nodes"])if(node.value("name","")=="ClearSphere")node["scale"]={1.2,1,.7};
        std::ofstream(fixtures/"Model/BSDFMaterials.gltf")<<scaled.dump(2);
        auto nonuniform=LoadBSDFScene(fixtures/"scene.json");
        Require(CameraMedium(nonuniform,0,{-1.25f,.65f,-.95f})>0,"nonuniform volume containment");
        auto overlap=model;Json center;
        for(const auto &node:model["nodes"])if(node.value("name","")=="ClearSphere")center=node["translation"];
        for(auto &node:overlap["nodes"])if(node.value("name","")=="RoughSphere")node["translation"]=center;
        std::ofstream(fixtures/"Model/BSDFMaterials.gltf")<<overlap.dump(2);
        rejected=false;try{LoadBSDFScene(fixtures/"scene.json");}catch(const std::exception&){rejected=true;}
        Require(rejected,"overlapping volume bounds must fail");
        std::ofstream(fixtures/"Model/BSDFMaterials.gltf")<<model.dump(2);
        std::cout<<"BSDF CPU profile, immutable Room composition, materials, inside/outside and absorption PASS\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
