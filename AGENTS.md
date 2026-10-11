# Shader.jp graphics tutorial instructions

- Work only in this repository. `D:\Git\DirectX-Graphics-Samples2026` is a read-only Microsoft implementation reference; `D:\Git\Shaderjp` contains the website and is changed only on a separate article request.
- Use a single Visual Studio 2026 solution, native `.vcxproj` applications, v145, Windows SDK 10.0.26100.0, C++20, x64 Debug/Release. Add a separate executable project for each technique/API.
- Use a 1280x720 initial client area for every sample, configured in `Common/Application.cpp`. The minimize/restore test returns to this size. Keep existing 800x600 documentation/article captures unchanged; capture new article images at 1280x720.
- Keep graphics initialization, resource creation, drawing, synchronization, and teardown visible in each sample. `Common` is for Win32 lifecycle, CPU data/file/image/model loading, and math, not a shared rendering API.
- D3D12 uses Agility SDK 1.619.6 and DXC 1.9.2609.5 from NuGet. Vulkan uses the installed Vulkan SDK, including its DXC, and targets Vulkan 1.3.
- Share HLSL source; compile offline with DXC, Shader Model 6.6, HLSL 2021. Preserve register/binding conventions (b0=0, t0=1, s0=2, set 0), row-major matrices, left-handed camera, depth 0..1, and Vulkan's negative-height viewport.
- Use WIC for the initial PNG samples. Manage future non-SDK dependencies as pinned Git submodules under `ThirdParty`; introduce DirectXTex/imgui only with the corresponding later tutorial.
- Provide Japanese Markdown under `Docs/<API>` and real rendered screenshots when available. Do not publish or push unless requested.
- Sample 09 pins Dear ImGui v1.92.9b at `f1cc2ae15e53a861a874c3034aae6798fde194ab` under `ThirdParty/imgui`. Keep the vendor sources unmodified. Use its Win32 platform backend; the per-API renderers use shared offline HLSL and a static font atlas. Shared widgets live in `Samples/Shared`, not in a common graphics API.

## Required Visual Studio filters

- Keep every `.vcxproj` and its matching `.vcxproj.filters` synchronized when adding, removing, renaming, or moving project-visible files. Create a filters file when missing.
- Assign every filterable project item, including C++, headers, HLSL, resources, manifests and build scripts, to exactly one declared logical filter. Do not leave items unfiltered.
- Reuse `Source Files`, `Header Files`, `Shaders`, `Resource Files`, `Build Scripts`, and existing subfilters. Declare all parent filters and give every new filter a globally unique GUID.
- Filters organize the IDE; do not move physical files just to match them. Do not retarget VS/SDK/toolset just to adjust filters.
- Before completing changes, parse both XML files and verify no missing/extra items, duplicate mappings, or undeclared filter references. Run `Tools/ValidateProjects.ps1` and relevant VS/MSBuild checks.

## Validation

- Restore: `msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true`.
- Build x64 Debug and Release; the build also validates Vulkan shader binaries with `spirv-val`.
- Run `Tools/TestLoaders.ps1` for OBJ/WIC/file handling and `Tools/TestSamples.ps1` for GPU rendering, resize, minimize/restore, capture, and shutdown. Debug runs must enable the graphics validation layers and report zero validation errors.
- Capture documentation images with `Tools/TestSamples.ps1 -CaptureDocs`, inspect them, and record exact tested compiler/SDK/GPU versions in `Docs/Verification.md`.
- For Sample 09, run `Tools/TestImGui.ps1` in Debug and Release. It exercises slider/Reset input and verifies that GPU scene pixels change and return to their baseline. Uniform/constant and UI buffers must only be overwritten after the GPU completion wait.
