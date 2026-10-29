# 発展編04 Mesh Shader基礎 — Direct3D12

既存のAlbedo用ShaderBallを描画します。Vertex Shader側の処理をMesh Shaderへ置き換え、Pixel ShaderでAlbedo×Base Color Factorを出力します。照明、IBL、HDRの露出、Bloom、Meshlet分割とカリングは加えません。

![実GPUの完成表示](Images/Advanced04_MeshShaderBasic-base.png)


```powershell
msbuild Samples\Direct3D12\Advanced04_MeshShaderBasic\Direct3D12_Advanced04_MeshShaderBasic.vcxproj -p:Configuration=Debug -p:Platform=x64
bin\x64\Debug\Direct3D12_Advanced04_MeshShaderBasic\Direct3D12_Advanced04_MeshShaderBasic.exe
```


## 使用するAPIと事前コンパイル

D3D12はAgility SDK 1.620.1、NuGet DXC 1.9.2609.5、SM6.6を使用します。Mesh Shader Tierを起動時に問い合わせます。Pipeline State StreamにMS／PSを設定し、通常描画は`ID3D12GraphicsCommandList6::DispatchMesh`で起動します。

VS 2026、v145、Windows SDK 10.0.26100.0、C++20、x64 Debug／Releaseを維持します。共通HLSLは`Shaders/MeshBasic`にあります。GPUリソース、Pipeline、描画、Barrier、Fence／Semaphore、終了時の解放はこのプロジェクトの`Sample.cpp`で追えます。共有C++はCPUデータとウィジェットです。

## Shaderを読み、Mesh Pipelineを起動する

プロジェクトのCustomBuildは`Mesh.hlsl`の`MSMain`を`ms_6_6`、`Pixel.hlsl`の`PSMain`を`ps_6_6`へコンパイルし、`Shaders/Mesh.cso`と`Pixel.cso`を配布します。実行時にDXCを起動せず、`ReadBinary`でこのDXILを読みます。VS用の入力レイアウトとMesh出力の頂点配列は別のインターフェースです。

`CreatePipeline`ではRoot Signature、MS／PSのBytecode、Rasterizer、Depth、Render Target形式をPipeline State Streamへ置き、`ID3D12Device2::CreatePipelineState`でMesh用PSOを作ります。Vertex reference用PSOも同じPixel Shaderと出力形式を使います。

```cpp
commands_->SetPipelineState(panel_.vertex ? pipeline_.Get() : meshPipeline_.Get());
// Root Signature、Scene定数、Texture、頂点／Index SRVを設定した後
meshCommands_->DispatchMesh((drawCount_ / 3 + 31) / 32, 1, 1);
```

Mesh ShaderにはInput Assemblerからの頂点入力がありません。Root Signatureで渡したStructuredBufferをシェーダ自身が読みます。b0／binding 0はScene、t0／1はAlbedo、s0／2はSampler、t1／3は頂点、t2／4はIndexです。`OnRender`のGPU完了待ち後に定数を更新し、描画、表示、UI、Fenceの順を追えます。

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
