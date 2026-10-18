#pragma once
#include "BindlessData.h"
#include "CameraNavigation.h"
#include <imgui.h>

namespace tutorial
{
struct GalleryPanel
{
    CameraNavigation navigation;
    int binding = 1, camera = 0, view = 0;
    bool driven = false, shadows = true, reflections = true, culling = true, visible = true, reset = false;
    uint32_t objects = 512, materials = 64, testFrame = 0;
    bool test = false;
    int initialTestBinding = 1;
    std::filesystem::path testScreenshot;
    ImVec2 bindingPoint{}, resetPoint{}, choices[3]{};
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
    ImVec2 inlineShadowPoint{};
#endif
    void QueueTestInput()
    {
        if (!test)
            return;
        ++testFrame;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
        if(testFrame==8 || testFrame==24) {
            auto point=testFrame==8 ? inlineShadowPoint : resetPoint;
            io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(0,true);
        }
        if(testFrame==9 || testFrame==25)io.AddMouseButtonEvent(0,false);
#else
        if (testFrame == 8 || testFrame == 12 || testFrame == 24)
        {
            const auto point = testFrame == 8    ? bindingPoint
                               : testFrame == 12 ? choices[initialTestBinding == 0 ? 1 : 0]
                                                 : resetPoint;
            io.AddMousePosEvent(point.x, point.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (testFrame == 9 || testFrame == 13 || testFrame == 25)
            io.AddMouseButtonEvent(0, false);
#endif
    }
    std::filesystem::path TestCapture() const
    {
        if (!test)
            return {};
        const wchar_t *suffix = testFrame == 6    ? L"-baseline"
                                : testFrame == 18 ?
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
                                      L"-edited"
#else
                                      L"-switched"
#endif
                                : testFrame == 30 ? L"-reset"
                                                  : nullptr;
        return suffix ? testScreenshot.parent_path() / (testScreenshot.stem().wstring() + suffix + L".png")
                      : std::filesystem::path{};
    }
    void Initialize(const GalleryData &d, const Options &o)
    {
        if (testFrame == 0)
        {
            test = o.testUi;
            testScreenshot = o.screenshot;
            initialTestBinding = o.binding == L"bound" ? 0 : o.binding == L"textures" ? 1 : 2;
            if (test && (o.hideUi || o.benchmark || o.frames < 36 || o.screenshot.empty()))
                throw std::runtime_error("--test-ui requires visible UI, --frames >= 36 and --screenshot");
        }
        binding = o.binding == L"bound" ? 0 : o.binding == L"textures" ? 1 : 2;
        if (GALLERY_TECHNIQUE == 12 && binding == 2)
            throw std::runtime_error("Advanced12 supports bound / textures");
        driven = o.galleryGpuDriven;
        shadows = !o.disableShadows;
        reflections = !o.disableReflections;
        visible = !o.hideUi;
        if (driven && (GALLERY_TECHNIQUE == 12 || GALLERY_TECHNIQUE == 14 || !binding))
            throw std::runtime_error("GPU driven requires Mesh Shader and a bindless mode");
        if (!o.meshCulling.empty() && o.meshCulling != L"off" && o.meshCulling != L"frustum")
            throw std::runtime_error("Gallery culling: off / frustum");
        culling = o.meshCulling != L"off";
        objects = d.objects;
        materials = d.materialCount;
        view = o.galleryView == L"material" ? 1 : o.galleryView == L"geometry" ? 2 : o.galleryView == L"uv" ? 3 : 0;
        std::string name = o.camera.empty() ? d.preset + "Hero" : std::filesystem::path(o.camera).string();
        auto it = std::find_if(d.source.cameras.begin(), d.source.cameras.end(),
                               [&](const auto &c) { return c.name == name; });
        if (it == d.source.cameras.end())
            throw std::runtime_error("Unknown Gallery camera");
        camera = int(it - d.source.cameras.begin());
        navigation.test = o.testCamera;
    }
    void Draw(const GalleryData &d, const char *api, double cpu, double gpu)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({325, 440}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Bindless Gallery");
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
        ImGui::Text("%s / Advanced 16 / RayQuery", api);
#else
        ImGui::Text("%s / Advanced %d", api, GALLERY_TECHNIQUE);
#endif
        const char *modes[] = {"Bound", "Bindless textures", "Bindless resources"};
        bool open = ImGui::BeginCombo("Binding", modes[binding]);
        auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        bindingPoint = {(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f};
        if (open)
        {
            for (int i = 0; i < (GALLERY_TECHNIQUE == 12 ? 2 : 3); ++i)
            {
                if (ImGui::Selectable(modes[i], binding == i))
                {
                    binding = i;
                    if (!i)
                        driven = false;
                }
                lo = ImGui::GetItemRectMin();
                hi = ImGui::GetItemRectMax();
                choices[i] = {(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f};
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Objects", std::to_string(objects).c_str()))
        {
            for (auto value : {128u, 512u, 4096u})
                if (ImGui::Selectable(std::to_string(value).c_str(), objects == value))
                    objects = value;
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Materials", std::to_string(materials).c_str()))
        {
            for (auto value : {16u, 64u, 256u})
                if (ImGui::Selectable(std::to_string(value).c_str(), materials == value))
                    materials = value;
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Camera", d.source.cameras[camera].name.c_str()))
        {
            for (int i = 0; i < int(d.source.cameras.size()); ++i)
                if (ImGui::Selectable(d.source.cameras[i].name.c_str(), i == camera))
                {
                    camera = i;
                    navigation.Reset();
                }
            ImGui::EndCombo();
        }
        const char *views[] = {"Final", "Material ID", "Geometry ID", "UV"};
        ImGui::Combo("View", &view, views, 4);
        if (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
        {
            ImGui::BeginDisabled(binding == 0);
            ImGui::Checkbox("GPU driven", &driven);
            ImGui::EndDisabled();
            ImGui::Checkbox("Frustum culling", &culling);
        }
        if (GALLERY_TECHNIQUE >= 14)
        {
            ImGui::Checkbox("RT shadows", &shadows);
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
            {auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();inlineShadowPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
#endif
            ImGui::Checkbox("RT reflections", &reflections);
        }
        reset = ImGui::Button("Reset");
        lo = ImGui::GetItemRectMin();
        hi = ImGui::GetItemRectMax();
        resetPoint = {(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f};
        if (testFrame == 18)
        {
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
            if(shadows)throw std::runtime_error("Inline shadow UI input failed");
            Trace("Inline shadow UI switch PASS");
#else
            if (binding != (initialTestBinding == 0 ? 1 : 0))
                throw std::runtime_error("Binding UI input failed");
            Trace("Bindless UI binding switch PASS");
#endif
        }
        if (testFrame == 30)
        {
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
            if(!shadows)throw std::runtime_error("Inline shadow UI Reset failed");
#endif
            if (binding != initialTestBinding)
                throw std::runtime_error("Binding UI Reset failed");
            Trace("Bindless UI Reset PASS");
        }
        ImGui::Text("%u objects / %u materials", d.objects, d.materialCount);
        ImGui::Text("%zu images / %zu candidates", d.images.size(), d.candidates.size());
        ImGui::Text("CPU record %.3f ms / GPU %.3f ms", cpu, gpu);
        navigation.Hint();
        ImGui::End();
    }
};

struct GalleryTimings
{
    static constexpr const char *CpuNames[] = {"update", "record", "submit", "gpu-wait", "present"};
    static constexpr const char *GpuNames[] = {"cull", "geometry", "rt", "lighting", "display", "total"};
    std::array<double, 5> cpu{};
    std::array<double, 6> gpu{};
    std::vector<std::array<double, 11>> samples;
    uint32_t frame = 0;
    uint64_t draws = 0, dispatches = 0, binds = 0, imageBytes = 0, bufferBytes = 0, descriptorBytes = 0, asBytes = 0;
    uint64_t committedImages = 0, committedBuffers = 0, descriptorCount = 0, imageResources = 0;
    uint32_t visibleInstances = 0, visibleGeometries = 0, gpuVisibleMeshlets = 0, frustumInstances = 0,
             frustumMeshlets = 0;
    uint64_t sceneBufferResources = 0, attachmentCommitted = 0;
    double loadMs = 0, uploadMs = 0, asBuildMs = 0;
    std::string api, gpuName, driver, presentMode;
    void Record(bool benchmark)
    {
        if (benchmark && frame >= 60 && frame < 1260)
        {
            std::array<double, 11> row;
            std::copy(cpu.begin(), cpu.end(), row.begin());
            std::copy(gpu.begin(), gpu.end(), row.begin() + 5);
            samples.push_back(row);
        }
        ++frame;
    }
    void Save(const Options &o, const GalleryData &d, const GalleryPanel &p) const
    {
        if (o.timings.empty())
            return;
        nlohmann::json j = {{"api", api},
                            {"gpuName", gpuName},
                            {"driver", driver},
                            {"presentMode", presentMode},
                            {"technique", GALLERY_TECHNIQUE},
                            {"binding", std::array{"bound", "textures", "resources"}[p.binding]},
                            {"gpuDriven", p.driven},
                            {"comparison", p.driven                  ? "submission-architecture"
                                           : GALLERY_TECHNIQUE >= 14 ? "storage-and-access"
                                                                     : "matched-submission"},
                            {"objects", d.instances.size()},
                            {"residentMaterials", d.materials.size()},
                            {"images", d.images.size()},
                            {"geometries", d.geometries.size()},
                            {"assetHash", d.assetHash},
                            {"width", 1280},
                            {"height", 720},
                            {"warmupFrames", 60},
                            {"measuredFrames", samples.size()},
                            {"fixedDelta", 1.0 / 60},
                            {"draws", draws},
                            {"dispatches", dispatches},
                            {"descriptorBinds", binds},
                            {"descriptorUpdatesPerFrame", 0},
                            {"memory",
                             {{"images", imageBytes},
                              {"buffers", bufferBytes},
                              {"descriptors", descriptorBytes},
                              {"accelerationStructures", asBytes}}},
                            {"startupMs", {{"load", loadMs}, {"upload", uploadMs}, {"asBuild", asBuildMs}}},
                            {"msvcFullVersion", _MSC_FULL_VER},
                            {"windowsSdk", "10.0.26100.0"},
                            {"rtShadows", p.shadows},
                            {"rtReflections", p.reflections},
                            {"culling", p.culling}};
#ifdef _DEBUG
        j["configuration"] = "Debug";
        j["validation"] = true;
#else
        j["configuration"] = "Release";
        j["validation"] = false;
#endif
        j["sceneBufferResources"] = sceneBufferResources;
#if defined(INLINE_SAMPLE) && INLINE_SAMPLE == 16
        j["technique"] = 16;
        j["rayTracingExecution"] = "RayQuery";
#endif
        j["memory"]["attachmentPayload"] = uint64_t(1280) * 720 * 84;
        j["memory"]["attachmentsCommitted"] = attachmentCommitted;
        j["memory"]["scope"] = "asset images, scene buffers, five RGBA32F + D32 attachments and AS results; excludes "
                               "swapchain, UI, query pools and transient upload/AS scratch";
        j["memory"]["imagesCommitted"] = committedImages;
        j["memory"]["sceneBuffersCommitted"] = committedBuffers;
        if (api == "Vulkan")
            j["memory"]["descriptors"] = nullptr;
        j["descriptorCount"] = descriptorCount;
        j["imageResources"] = imageResources;
        j["visibility"] = {{"primaryInstances", visibleInstances},
                           {"primaryGeometries", visibleGeometries},
                           {"frustumInstancesCpuReference", frustumInstances},
                           {"frustumMeshletsCpuReference", frustumMeshlets},
                           {"meshletsGpu", p.driven ? nlohmann::json(gpuVisibleMeshlets) : nlohmann::json(nullptr)}};
        std::set<uint32_t> used;
        for (auto instance : d.instances)
            used.insert(instance.ids.y);
        j["usedMaterials"] = used.size();
        for (size_t c = 0; c < 11; ++c)
        {
            std::vector<double> v;
            for (auto r : samples)
                v.push_back(r[c]);
            if (v.empty())
                continue;
            std::sort(v.begin(), v.end());
            j[c < 5 ? "cpu" : "gpu"][c < 5 ? CpuNames[c] : GpuNames[c - 5]] = {
                {"averageMs", std::accumulate(v.begin(), v.end(), 0.0) / v.size()},
                {"medianMs", v[v.size() / 2]},
                {"p95Ms", v[std::min(v.size() - 1, size_t(v.size() * .95))]}};
        }
        std::filesystem::create_directories(o.timings.parent_path());
        std::ofstream(o.timings) << j.dump(2);
        auto path = o.timings;
        path.replace_extension(".csv");
        std::ofstream csv(path);
        csv << "frame";
        for (auto n : CpuNames)
            csv << ",cpu-" << n;
        for (auto n : GpuNames)
            csv << ",gpu-" << n;
        csv << '\n';
        for (size_t i = 0; i < samples.size(); ++i)
        {
            csv << i;
            for (double v : samples[i])
                csv << ',' << v;
            csv << '\n';
        }
    }
};
} // namespace tutorial
