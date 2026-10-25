#pragma once
#include "PathTracingPanel.h"
#include "BSDFData.h"
#include <cstring>
namespace tutorial
{
struct BSDFPanel : PathTracingPanel
{
    int mode=0, selected=0, initialScene=0;
    int initialMode=0, initialSelected=0, initialCamera=0, initialBounces=16, initialSampling=1;
    bool initialPaused=false;
    uint32_t revision=0;
    mutable uint32_t initialMedium=0;
    bool materialsDirty=true;
    std::vector<WorkshopMaterial> savedMaterials;
    std::vector<uint32_t> glassMaterials;
    ImVec2 modePoints[3]{};
    mutable DirectX::XMFLOAT3 lastEye{FLT_MAX,FLT_MAX,FLT_MAX};
    mutable int mediumScene=-1,mediumMode=-1;
    uint32_t diagnostic=0;
    const char *ModeName() const { return mode==0 ? "full" : mode==1 ? "reflection" : "legacy-pbr"; }
    void CameraForPreset(const Workshop &w)
    {
        navigation.Reset();
        for(int i=0;i<int(w.cameras.size());++i)if(w.cameras[i].name==(scene==0?"Lab":"Thickness"))camera=i;
    }
    void Initialize(Workshop &w,const Options &o)
    {
        Options defaults=o;
        if(defaults.camera.empty())defaults.camera=o.bsdfTest==L"tir"?L"InsideClear":o.bsdfPreset==L"thickness"?L"Thickness":L"Lab";
        PathTracingPanel::Initialize(w,defaults);
        scene=initialScene=o.bsdfPreset==L"thickness" || o.bsdfTest==L"slab" || o.bsdfTest==L"scaled"?1:0;
        if(o.bsdfTest==L"slab" || o.bsdfTest==L"scaled")CameraForPreset(w);
        diagnostic=o.bsdfTest==L"optics"?1:o.bsdfTest==L"furnace"?2:o.bsdfTest==L"slab" || o.bsdfTest==L"scaled"?3:0;
        mode=o.bsdfMode==L"full"?0:o.bsdfMode==L"reflection"?1:2;
        for(uint32_t i=0;i<w.materials.size();++i)if(w.materials[i].transmission>0)glassMaterials.push_back(i);
        if(glassMaterials.empty())throw std::runtime_error("BSDF scene requires dielectric materials");
        bool found=false;
        for(int i=0;i<int(glassMaterials.size());++i)if(w.materials[glassMaterials[i]].id==std::filesystem::path(o.bsdfMaterial).string()){selected=i;found=true;}
        if(!found)throw std::runtime_error("unknown --bsdf-material stable ID");
        auto &material=w.materials[glassMaterials[selected]];
        if(o.bsdfIor>=0)material.ior=o.bsdfIor;
        if(o.bsdfRoughness>=0)material.factors.y=o.bsdfRoughness;
        if(o.bsdfAbsorptionDistance>=0)material.attenuationDistance=o.bsdfAbsorptionDistance;
        savedMaterials=w.materials;
        intensity=w.environmentIntensity;bias=.0001f;
        initialMode=mode;initialSelected=selected;initialCamera=camera;
        initialBounces=bounces;initialSampling=sampling;initialPaused=paused;
    }
    void QueueTestInput()
    {
        PathTracingPanel::QueueTestInput();
        if(!test)return;
        for(int i=0;i<3;++i) {
            const uint32_t f=54+6*uint32_t(i);
            if(frame==f || frame==f+1) {
                const auto p=modePoints[(i+1)%3];auto &io=ImGui::GetIO();
                io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,frame==f);
            }
        }
    }
    std::filesystem::path TestCapture() const
    {
        if(!test || screenshot.empty())return {};
        const wchar_t *suffix=frame==59?L"-reflection":frame==65?L"-legacy":frame==73?L"-full-again":nullptr;
        if(suffix && (!targetSpp || samples>=targetSpp))return screenshot.parent_path()/(screenshot.stem().wstring()+suffix+L".png");
        return PathTracingPanel::TestCapture();
    }
    void Draw(Workshop &w,const char *api)
    {
        if(!visible)return;
        ImGui::SetNextWindowPos({10,10},ImGuiCond_Always);
        ImGui::SetNextWindowSize({380,880},ImGuiCond_FirstUseEver);
        ImGui::Begin("BRDF / BSDF - Advanced 19");
        ImGui::Text("%s / %u spp",api,samples);
        const char *names[]={"BSDF: reflection + transmission","BRDF: reflection component only","Legacy opaque PBR fallback"};
        for(int i=0;i<3;++i) {
            ImGui::RadioButton(names[i],&mode,i);
            auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();modePoints[i]={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
        }
        const char *scenes[]={"Lab","Thickness"};
        if(ImGui::Combo("Preset",&scene,scenes,2))CameraForPreset(w);
        if(ImGui::BeginCombo("Camera",w.cameras.at(camera).name.c_str())) {
            for(int i=0;i<int(w.cameras.size());++i)if(ImGui::Selectable(w.cameras[i].name.c_str(),camera==i)){camera=i;navigation.Reset();}
            ImGui::EndCombo();
        }
        if(ImGui::BeginCombo("Glass material",w.materials[glassMaterials[selected]].id.c_str())) {
            for(int i=0;i<int(glassMaterials.size());++i)if(ImGui::Selectable(w.materials[glassMaterials[i]].id.c_str(),selected==i))selected=i;
            ImGui::EndCombo();
        }
        auto &m=w.materials[glassMaterials[selected]];
        bool edited=ImGui::SliderFloat("IOR",&m.ior,1,3,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();editPoint={a.x+180,(a.y+b.y)*.5f};
        edited|=ImGui::SliderFloat("Roughness",&m.factors.y,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        edited|=ImGui::ColorEdit3("Absorption color",&m.attenuationColor.x,ImGuiColorEditFlags_Float);
        edited|=ImGui::SliderFloat("Distance (m)",&m.attenuationDistance,0,3,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextUnformatted("Distance 0: no absorption / roughness 0: delta");
        if(mode==2)ImGui::TextUnformatted("Fallback ignores IOR, transmission, absorption");
        if(edited){++revision;materialsDirty=true;}
        ImGui::SliderInt("Max bounces",&bounces,1,32,"%d",ImGuiSliderFlags_AlwaysClamp);
        if(ImGui::CollapsingHeader("Direct-light sampling")) {
            const char *samplers[]={"BSDF directions only","NEE + MIS"};ImGui::Combo("Estimator",&sampling,samplers,2);
        }
        ImGui::SliderFloat("Exposure",&exposure,.1f,4,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();exposurePoint={a.x+180,(a.y+b.y)*.5f};
        ImGui::Checkbox("Pause accumulation",&paused);
        a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();pausePoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
        if(ImGui::Button("Reset accumulation"))resetRequested=true;
        if(ImGui::Button("Reset all")) {
            w.materials=savedMaterials;mode=initialMode;scene=initialScene;selected=initialSelected;revision=0;materialsDirty=true;
            bounces=initialBounces;sampling=initialSampling;exposure=1;paused=initialPaused;
            camera=initialCamera;navigation.Reset();resetRequested=true;
        }
        a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();resetPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
        ImGui::TextUnformatted("1920 x 1080 / 1 sample per frame");navigation.Hint();ImGui::End();
    }
    BSDFConstants Constants(const Workshop &w,uint32_t width,uint32_t height,uint32_t sample,uint32_t lightCount) const
    {
        const auto base=PathTracingPanel::Constants(w,width,height,sample,lightCount);
        BSDFConstants c{};std::memcpy(&c,&base,sizeof(base));
        DirectX::XMFLOAT3 eye{c.camera.x,c.camera.y,c.camera.z};
        if(mediumScene!=scene || mediumMode!=mode || std::memcmp(&lastEye,&eye,sizeof(eye))) {
            initialMedium=mode==2?0:CameraMedium(w,scene,eye);lastEye=eye;mediumScene=scene;mediumMode=mode;
        }
        c.bsdf={uint32_t(mode),initialMedium,revision,diagnostic};
        return c;
    }
};
} // namespace tutorial
