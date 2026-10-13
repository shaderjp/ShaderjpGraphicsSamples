# 発展編04 Mesh Shader基礎 — Vulkan

既存のAlbedo用ShaderBallを描画します。Vertex Shader側の処理をMesh Shaderへ置き換え、Pixel ShaderでAlbedo×Base Color Factorを出力します。照明、IBL、HDRの露出、Bloom、Meshlet分割とカリングは加えません。

![実GPUの完成表示](Images/Advanced04_MeshShaderBasic-base.png)


```powershell
msbuild Samples\Vulkan\Advanced04_MeshShaderBasic\Vulkan_Advanced04_MeshShaderBasic.vcxproj -p:Configuration=Debug -p:Platform=x64
bin\x64\Debug\Vulkan_Advanced04_MeshShaderBasic\Vulkan_Advanced04_MeshShaderBasic.exe
```


## 使用するAPIと事前コンパイル

Vulkan 1.3とSDK付属DXCを使用します。`VK_EXT_mesh_shader`と`meshShader`、必要な出力・グループ上限を起動時に確認します。共通HLSLを`SPV_EXT_mesh_shader`へコンパイルし、NVのMesh Shader経路は使いません。通常描画は`vkCmdDrawMeshTasksEXT`です。負の高さのViewport、左手系、Depth 0〜1を維持します。

VS 2026、v145、Windows SDK 10.0.26100.0、C++20、x64 Debug／Releaseを維持します。共通HLSLは`Shaders/MeshBasic`にあります。GPUリソース、Pipeline、描画、Barrier、Fence／Semaphore、終了時の解放はこのプロジェクトの`Sample.cpp`で追えます。共有C++はCPUデータとウィジェットです。

## Shaderを読み、Mesh Pipelineを起動する

プロジェクトのCustomBuildは`Mesh.hlsl`の`MSMain`を`ms_6_6`、`Pixel.hlsl`の`PSMain`を`ps_6_6`として、`-spirv`と`SPV_EXT_mesh_shader`で事前コンパイルします。`ReadBinary`で配布した`Shaders/Mesh.spv`を読み、`vkCreateShaderModule`へ渡します。実行時にDXCを起動しません。

```cpp
stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
stages[0].module = meshShader_;
stages[0].pName = "MSMain";
info.pVertexInputState = nullptr;
info.pInputAssemblyState = nullptr;
```

Fragment StageとDepth／Colorの形式を揃えて`vkCreateGraphicsPipelines`を呼びます。Mesh PipelineにはVertex Input／Input Assembly Stateがありません。`OnRender`ではPipelineとDescriptor SetをBindして、取得したEXT関数ポインタから`drawMesh_(commands_, (drawCount_ / 3 + 31) / 32, 1, 1)`を呼びます。

set 0のbinding 0はScene、1はAlbedo、2はSampler、3は頂点、4はIndexです。頂点とIndexはStorage Bufferとして渡し、シェーダ自身が読みます。GPU完了待ち後の定数更新、Image Layout遷移、表示、Semaphore／Fence、終了時の解放はこのアプリにあります。

## 頂点と三角形をまとめて出力する

この回では元のIndex列を32三角形ずつ読みます。Thread Groupは32スレッド、1スレッドは1三角形の3頂点を変換します。共有されていた頂点も重複して出力し、最大96頂点・32三角形をラスタライザへ渡します。最終グループは残った三角形数だけ出力します。

`SetMeshOutputCounts`で実際の頂点数と三角形数を設定し、出力配列を記入します。局所的な接続は`uint3(thread*3, thread*3+1, thread*3+2)`です。Meshletによる頂点共有は次の共通記事で導入します。

GPU頂点は2つのfloat4、32 bytesです。位置＋U、法線＋Vを持ち、この回のPixel Shaderが読むのはUVだけです。Scene定数はrow-majorのMVP、Base Color FactorとIndex数を持つ96 bytesです。

## Pixel Shaderと表示変換

PNGをWICで読み、sRGBのTexture Viewを使います。Sampleした線形AlbedoへBase Color Factorを掛け、sRGB Render Targetで表示時の変換を行います。照明係数やReinhard変換は掛けません。背景は単色です。

Vertex referenceでは同じGPU頂点・Index・Pixel Shaderを使います。出力される表面を揃え、Mesh Shaderに置き換えた結果を比較できます。


## 操作と比較

全サンプルでWASD移動、Q/E上下移動、Shift加速、右ドラッグ回転、RのカメラResetを使用できます。ImGui操作中やフォーカスを失ったときにはカメラ入力を解放します。初期解像度は1280×720です。 Vertex Shader referenceのチェックで従来経路を比較できます。Model X、Camera path、Resetも使用できます。

CLIは`--render-path vertex|mesh`を使用します。既存の`--no-ui`、`--frames`、`--test-odd`、`--test-camera`、`--screenshot`、`--capture-gpu`も継承します。04はPNGの画像読戻し、05以降はG-buffer／HDR・Meshletリストの診断読戻しを行います。デバッグ用の既知形状は`--mesh-test-scene`で選びます。

## 検証と性能の読み方

`Tools/TestMeshlets.ps1`はCPUで元の全三角形、局所Indexと境界を検証します。`Tools/TestMeshShaders.ps1`は実GPUで各経路、カリング、材質、UI・Resetとリサイズを検証します。`Tools/TestMeshShaderBinaries.ps1`はDXIL／SPIR-Vと配置を照合します。

05以降の性能はRelease、同一のStandard／Stress、固定カメラ経路、UI・VSyncなしで測定します。60フレーム準備後の1200フレームを記録し、管理・判定の追加コストを含むTotalも比較します。カリングで候補が減ることと、全体が速くなることを別々に報告します。AS／Taskの時間はGeometryへ含みます。

[Meshletとmeshoptimizerの共通章](../Meshlets.md)・[検証記録](../MeshShaderVerification.md)・[発展編目次](../AdvancedGraphicsSamples.md)
