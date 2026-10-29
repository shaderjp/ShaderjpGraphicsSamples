# Direct3D12 第10回 CubemapとHDR環境を方向で参照する

第9回までの材質とUIに、周囲を参照するCubemapを加えます。6面の方向を診断し、HDRのSkyboxと理想鏡面を表示します。この回の鏡面表示はRoughnessを無視した方向確認です。PBRの環境反射は第12回で完成させます。

![Direct3D12の実GPU描画](Images/10_Cubemap-mirror-studio-0.png)

画像は1280×720の実GPUキャプチャです。形状と環境は[Lighting Labの自作アセット](../Assets/LightingLab.md)、反射の式と各回のつながりは[共通解説](../EnvironmentLighting.md)を参照してください。

## ビルドと標準の実行

Visual Studio 2026／v145、Windows SDK 10.0.26100.0、C++20、x64を使います。Agility SDK 1.620.1とDXC 1.9.2609.5をNuGetで復元します。 DirectXTexはmay2026の固定Submoduleで、CPUのDDS読込に使います。Dear ImGuiの版は第9回と同じです。

```powershell
git submodule update --init --recursive
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\10_Cubemap\Direct3D12_10_Cubemap.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_10_Cubemap\Direct3D12_10_Cubemap.exe
```

VSではDirect3D12_10_Cubemapをスタートアッププロジェクトにします。実行には配布済みglTF・PNG・DDSを使い、BlenderやIBLベイクは不要です。アセットは実行ファイル基準で探すので、作業ディレクトリを変えても同梱データを読めます。

## アセットを読み込みGPUへ配置する

[Sample.cpp](../../Samples/Direct3D12/10_Cubemap/Sample.cpp)のOnInitでSceneを読み、APIのデバイス・描画先・コマンド・同期を準備します。CreateSceneでは各モデルのVertex／Index Bufferと材質画像、環境の6面と各mip、BRDF LUTを作ります。環境のGPU転送とView作成もこのプロジェクト内にあります。

CPUのLoadLightingSceneは、各glTFを既存のLoadGltfPbrで読み、配置だけを別に保持します。モデルごとに1メッシュ・1材質のプロフィールです。LoadDdsはDirectXTexでDDSを展開し、数値形式・面・mip・バイト配置を返します。CommonはGPU Resourceを作りません。

`CreatePipeline`でb0のRoot CBVと、t0〜t8のSRV Descriptor Tableを作ります。s0は材質用のmip 0のSampler、s1は環境用の線形・mip補間Samplerです。各モデルに9個のSRVを並べ、描画ごとにそのモデルのTableを設定します。

`LoadDdsTexture`はCPUの各面・mipをD3D12のSubresourceへ対応させます。Texture2DのDepthOrArraySizeを6、MipLevelsを実データの数にし、GetCopyableFootprintsのRowPitchへ合わせてUpload Bufferへコピーします。CopyTextureRegionのSubresourceIndexはmip+face*mipCountです。転送後にCOPY_DESTからPIXEL_SHADER_RESOURCEへ遷移し、Fence完了を待ってUpload Bufferを解放します。

環境はTEXTURECUBEのSRV、BRDF LUTはTEXTURE2DのSRVです。RGBA16F／RG16Fの値をそのまま使い、sRGB変換しません。材質のBase ColorとEmissiveだけは従来どおりsRGBです。

モデルごとの480-byte定数は512-byte境界に置きます。フレーム先頭のFence完了後に全モデル分の定数と、環境切替に伴うSRVを書き換えます。UIの頂点・インデックスもGPU完了後に更新します。



## HLSLでこの回の照明を計算する

共有する[Pixel.hlsl](../../Shaders/LightingLab/Pixel.hlsl)と[Scene.hlsli](../../Shaders/LightingLab/Scene.hlsli)はSM6.6／HLSL 2021で事前コンパイルします。LAB_TECHNIQUE=10で、この回までの計算を有効にします。CPUとHLSLはWorld、ViewProjection、Normal Matrix、材質・光、逆ViewProjection、Light ViewProjection、環境とShadow設定の480-byte配置を揃えています。

Skyboxの頂点シェーダは、画面全体を覆う三角形を`SV_VertexID`から作ります。NDCの遠端をinverse ViewProjectionでワールドへ戻し、カメラ位置との差から参照方向を求めます。Skyboxは深度1、LESS_EQUAL、深度書込みなしです。その後に通常のモデルをLESS・深度書込みありで描きます。

表面の理想鏡面は`reflect(-V, N)`で環境を引きます。Vは表面からカメラへ向かう方向なので、入射方向として使う際に符号を反転します。反射とSkyboxの双方へ同じ環境回転を適用します。

## 表示を切り替えて観察する

`--shading mirror --environment orientation`では、+X／-X／+Y／-Y／+Z／-Zの面名と矢印を確認します。Environment yawを変えるとSkyboxと映り込みが同じ規約で回転します。StudioとRoomを切り替えると、白いパネルと暖色・寒色の光の違いが見えます。

![方向確認用環境の実GPU画像](Images/10_Cubemap-mirror-orientation-0.png)

この診断表示は`reflect(-V, N)`の方向から環境のmip 0を読みます。PBRのRoughnessやFresnelを含めた完成した照明と区別してください。Combinedは従来の方向光と材質の表示で、まだ拡散・鏡面IBLを足しません。

```powershell
& .\bin\x64\Debug\Direct3D12_10_Cubemap\Direct3D12_10_Cubemap.exe --shading mirror --environment studio --no-ui --frames 8 --screenshot ".\.cache\Direct3D12_10_Cubemap.png"
& .\bin\x64\Debug\Direct3D12_10_Cubemap\Direct3D12_10_Cubemap.exe --scene "Assets/Scenes/LightingLab/MaterialStudy.json" --environment room
```

`--scene`は指定した配置JSONへ、`--environment`はstudio／room／orientationへ切り替えます。`--no-ui`は撮影時にUIを隠します。環境強度、回転、直接光の強度、露出はUIで変更し、Resetで標準値へ戻します。

カメラの深度は引き続き0〜1、LESS比較、毎フレーム1へクリアします。リサイズではGPU完了後に描画先とカメラ用深度を作り直し、環境データと編集値は保持します。

## 検証と記事画像

```powershell
.\Tools\ValidateProjects.ps1
.\Tools\TestLightingLabAssets.ps1
.\Tools\TestLightingLab.ps1 -CaptureDocs
.\Tools\TestLightingLab.ps1 -Configuration Release
.\Tools\TestSamples.ps1 -Topics 10_Cubemap -CaptureDocs
```

TestLightingLabはCPUのDDS・配置読込と不正データの拒否、両APIの環境切替、IBLの寄与、Shadow・遮蔽物の移動、実ウィジェットの編集とResetを確認します。TestSamplesはリサイズ・最小化復帰・読戻し・終了を確認します。Debugではグラフィックス検証Layerを有効にし、Vulkanでは同期検証も使います。結果と環境は[検証記録](../Verification.md)を参照してください。

前：[第9回](09_ImGui.md)。 次：[第11回](11_DiffuseIBL.md)。 別API：[Vulkan 第10回](../Vulkan/10_Cubemap.md)。
