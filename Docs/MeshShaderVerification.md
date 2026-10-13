# 発展編04〜09 Mesh Shaderの検証記録

検証日：2026-10-14（日本時間）。Direct3D12／Vulkanの独立した12アプリ、API別12記事、[Meshletとmeshoptimizerの共通章](Meshlets.md)を追加しました。[発展編目次](AdvancedGraphicsSamples.md)から04、共通章、05 GPU Driven Rendering、06 AS／Task、07 Shadow、08 Deferred、09 Tiled Deferredの順に読めます。

## 環境

| 項目 | 実測・使用した値 |
|---|---|
| Visual Studio | VS 2026 Community 18.10.0／18.10.12201.205 |
| C++ | v145、C++20、MSVC 19.51.36257.0 |
| Windows SDK | 10.0.26100.0 |
| GPU／ドライバ | NVIDIA GeForce RTX 4070／610.88（32.0.16.1088） |
| D3D12 | Agility SDK 1.619.6、Mesh Shader Tier 1 |
| D3D12 DXC | NuGet 1.9.2609.5／01b62ad47 |
| Vulkan | Vulkan 1.3、SDK 1.4.341.1、`VK_EXT_mesh_shader` |
| Vulkan DXC | SDK付属 1.9.0.5180／e35541826 |
| HLSL | HLSL 2021、SM6.6、共通ソースの事前コンパイル |
| meshoptimizer | v1.3、`9e1f07b159d3cb777f1c67ed31fc11fd117986f4` |

Vulkanが報告したMesh出力上限は256頂点・256三角形、Task Payload上限は16,384 bytes、Meshの最大Invocation数は128です。04は32スレッド・96頂点・32三角形、05以降は64スレッド・64頂点・96三角形、AS／Taskは32スレッド・128-byte Payloadを使います。起動時にExtension／Feature、非一様なTexture参照、Descriptor数、出力・Payload・Group上限を検査します。未対応GPUそのものをこの環境で実機試験したわけではありません。

## ビルドとCPUデータ

- NuGet Restore、ソリューションのx64 Debug／Releaseビルドが成功しました。ソース確定後のビルドはコンパイラ警告・エラー0件です。ソリューションは58 APIアプリと2 CPUライブラリの計60プロジェクトです。
- `Tools/ValidateProjects.ps1`で全60組のvcxproj／filtersをXML解析し、項目の一対一対応、宣言済みFilter、親階層、GUID、ファイル実在とソリューションフォルダを確認しました。
- `Tools/TestMeshShaderBinaries.ps1`で両構成のDXILをdxv、SPIR-Vを`spirv-val --target-env vulkan1.3`で検証しました。VulkanはEXTのMesh／Taskで、NVのCapabilityは使用していません。
- CPUの`static_assert`とSPIR-VのStride／Offsetを照合しました。Vertex 48、Meshlet 64、Primitive 32、Material 80、Instance 144、MeshFrame 128 bytesです。04のVertexは32、Scene定数は96 bytesです。
- `Tools/TestMeshlets.ps1`がDebug／Releaseで成功しました。1,362個の固有Meshletと98,788個の元三角形について、向きを含む三角形集合の完全復元、局所Index範囲、64頂点・96三角形の上限、境界、決定性を確認しました。Instanceとの候補数はStandard 3,660、Stress 32,940です。
- 旧OBJ／WIC／FileローダーとWorkshop CPUローダーの試験が成功しました。既存アセット・編集正本と既存Submoduleのソースは変更していません。meshoptimizerの上流ソースも変更していません。

## GPU画像と可視性

両API・両構成で`Tools/TestMeshShaders.ps1`のGPU描画と、`Tests/MeshShaderGpuTests.py`の独立した数値計算を実行しました。05の2次元Indirect Dispatchは、256列の複数行と最後の端数行をStandard／Stressで追加確認しました。描画の可視数をCPUへ戻してから起動する経路はありません。

| 対象 | 確認した結果 |
|---|---|
| 04 Albedo | Mesh／Vertex referenceのPNG一致。既知UVと移動・回転・Scaleを独立したCPU投影と線形Texture Sample／sRGB計算へ照合 |
| Meshlet Culling | GPUの可視ID・件数をCPU double精度の球／視錐台・幾何法線コーン判定へ照合。空、全可視、片面の背面、端数グループも確認 |
| 材質 | MASKの穴と不透明部分、表／裏の両面表示。07〜09ではMASKのShadow Depth占有率も確認 |
| Shadow | カメラ外のCasterをMainから除外してもShadowへ残し、影OFFとの差が受光面へ現れることを確認 |
| Deferred | Vertex、直接Mesh、AS／Task、カリングOFF／視錐台のみとのDepth・HDR比較。全読戻しHDR／BloomにNaN／Infなし |
| Tiled | All／Tiled、容量1での全ライトFallbackをHDRで照合。4,680ライト、候補数表示と容量超過表示も実行 |
| UI | 実ウィジェットによる編集でUI外のシーンが変化し、Resetで基準に戻ることを確認 |
| リサイズ | 641×479、最小化、1280×720復帰、撮影・GPU読戻し・終了を確認 |

描画順が変わる比較では、Depth不連続の周囲1画素を除いたHDRを絶対差0.001＋相対差0.002で照合します。05には同じDepthに別の材質・法線・UVの面が重なる少数の画素がありました。その例外は64画素以下に制限し、独立したCPUのレイ／三角形交差計算で2つ以上の表面がDepth差0.0000002以内に存在することを確認しています。画像全体の最大差だけを一致判定には使っていません。

09のAtomicによるライトリスト順序は浮動小数点加算の丸めを変えます。ResetのHDRも上記範囲で照合し、最終8bit表示の差は最大1です。04〜08のUI外のReset画像は完全一致しました。

DebugはD3D12 Debug Layer／Vulkan Validation Layerと同期検証を有効にし、実行したケースのグラフィックス検証エラーは0件でした。`Tools/TestMeshShaderFailures.ps1`では全12アプリのFull、未対応描画経路、Shader欠落を両構成・各36ケースで確認し、明示的なエラー、終了コード1と部分初期化の解放を検証しました。

`Tools/TestSamples.ps1`をDebug／Releaseの全58アプリへ実行し、描画、撮影、リサイズ、最小化・1280×720復帰、終了が成功しました。新規12アプリの実際のWin32入力による移動・右ドラッグ回転・RのReset、既存09 ImGui、17・18 Deferred、19・20 Tiled／Clustered、発展編01〜03レイトレーシングの回帰も両構成で成功しました。検証エラーは0件です。

`TestCameraNavigationCpu.ps1`で座標基底、速度、Shift、UI入力、フォーカス・キャプチャ喪失、Pitch制限、Resetを再確認しました。GPU操作試験の05・09はScene定数のバイト一致と、可視性・HDRを上記の方法で検証します。それ以外の新規アプリはカメラResetのPNGが完全一致しました。

05〜09のStress専用ライフサイクルも両API・両構成の20ケースで成功しました。48フレームの実行中に641×479へ変更、最小化、1280×720へ復帰し、その後のDepth／HDR・可視性を再びCPUの数値試験へ照合しました。既定のGPU試験にこの手順を含め、`-StressLifecycleOnly`でも再実行できます。

## 性能測定の条件

Release、1280×720、同じStandard／Stressと固定カメラ経路、固定dt 1/60、OpaqueMask、UI／VSyncなしです。60フレーム準備後の1,200フレームを52条件で保存しました。性能測定中は別のサンプルを動かしていません。05〜08はWorkshopの8／72点光源、09は同じ配置へ追加した264／2,376点光源です。

GPUのTotalはTimestampで測るレンダリング区間で、PresentやCPU待機を含みません。05のCompute Cullingは`mesh-cull`へ分け、AS／TaskはGeometry／Shadowへ含めます。CPUは形状処理のコマンド記録区間だけで、アプリ全体のフレーム時間ではありません。AS／Taskの`mesh-cull=0`は独立測定していないという意味です。

以下の単位はms、中央は中央値、p95は95パーセンタイルです。平均・パス別・フレーム別の値は[生データ](Measurements/MeshShader/)のJSONとCSVへ保存しました。

### Direct3D12：05 Compute＋Indirect

| Scene | 経路 | Total中央 | Total p95 | Geometry中央 | Compute Culling中央 | CPU記録中央 |
|---|---|---:|---:|---:|---:|---:|
| Standard | vertex／off | 0.5376 | 0.7843 | 0.4905 | 0.0000 | 0.0334 |
| Standard | mesh／off | 0.5284 | 0.7997 | 0.4833 | 0.0000 | 0.0303 |
| Standard | indirect／off | 0.5612 | 0.7506 | 0.5100 | 0.0082 | 0.0071 |
| Standard | indirect／frustum-cone | 0.4833 | 0.6277 | 0.4209 | 0.0184 | 0.0066 |
| Stress | vertex／off | 2.5385 | 4.6746 | 2.4873 | 0.0000 | 0.2880 |
| Stress | mesh／off | 2.5108 | 4.3325 | 2.4617 | 0.0000 | 0.2521 |
| Stress | indirect／off | 2.1432 | 3.0915 | 2.0562 | 0.0389 | 0.0084 |
| Stress | indirect／frustum-cone | 2.2159 | 2.9338 | 2.1125 | 0.0573 | 0.0081 |

### Vulkan：05 Compute＋Indirect

| Scene | 経路 | Total中央 | Total p95 | Geometry中央 | Compute Culling中央 | CPU記録中央 |
|---|---|---:|---:|---:|---:|---:|
| Standard | vertex／off | 0.5045 | 0.7292 | 0.4516 | 0.0000 | 0.0330 |
| Standard | mesh／off | 0.5351 | 0.7820 | 0.4833 | 0.0000 | 0.0304 |
| Standard | indirect／off | 0.7088 | 0.8706 | 0.6513 | 0.0097 | 0.0051 |
| Standard | indirect／frustum-cone | 0.6077 | 0.7929 | 0.5407 | 0.0189 | 0.0047 |
| Stress | vertex／off | 2.4528 | 4.3663 | 2.4003 | 0.0000 | 0.2732 |
| Stress | mesh／off | 2.4430 | 4.8525 | 2.3921 | 0.0000 | 0.2522 |
| Stress | indirect／off | 2.0950 | 3.3560 | 2.0142 | 0.0295 | 0.0055 |
| Stress | indirect／frustum-cone | 2.2266 | 3.2241 | 2.1207 | 0.0542 | 0.0054 |

### 06〜09：AS／TaskのカリングOFFとON

| API | 回 | Scene | OFF Total中央 | ON Total中央 | ON Total p95 | ON CPU記録中央 |
|---|---:|---|---:|---:|---:|---:|
| Direct3D12 | 06 | Standard | 0.5386 | 0.4997 | 0.7864 | 0.0315 |
| Direct3D12 | 06 | Stress | 2.6542 | 2.3511 | 4.2383 | 0.2468 |
| Direct3D12 | 07 | Standard | 0.7240 | 0.6666 | 0.9308 | 0.0596 |
| Direct3D12 | 07 | Stress | 3.0484 | 2.9420 | 4.8241 | 0.4989 |
| Direct3D12 | 08 | Standard | 0.5806 | 0.5530 | 0.6943 | 0.0600 |
| Direct3D12 | 08 | Stress | 1.7203 | 1.5555 | 2.1268 | 0.4905 |
| Direct3D12 | 09 | Standard | 1.0250 | 0.9943 | 1.1981 | 0.0601 |
| Direct3D12 | 09 | Stress | 2.2221 | 2.0480 | 2.7750 | 0.4904 |
| Vulkan | 06 | Standard | 0.5612 | 0.5795 | 0.8722 | 0.0311 |
| Vulkan | 06 | Stress | 2.4965 | 2.2526 | 4.1067 | 0.2643 |
| Vulkan | 07 | Standard | 0.8789 | 0.7012 | 1.0153 | 0.0626 |
| Vulkan | 07 | Stress | 2.9712 | 2.8933 | 4.9386 | 0.5146 |
| Vulkan | 08 | Standard | 0.6499 | 0.7419 | 1.0157 | 0.0594 |
| Vulkan | 08 | Stress | 1.8238 | 1.5755 | 2.0386 | 0.4985 |
| Vulkan | 09 | Standard | 1.1743 | 1.2008 | 1.5399 | 0.0596 |
| Vulkan | 09 | Stress | 2.1261 | 2.2331 | 2.9403 | 0.5000 |

### 09：同じMesh CullingでAllとTiledを比較

| API | Scene | All Total中央 | Tiled Total中央 | All Total p95 | Tiled Total p95 |
|---|---|---:|---:|---:|---:|
| Direct3D12 | Standard | 2.2057 | 0.9943 | 2.8150 | 1.1981 |
| Direct3D12 | Stress | 17.9732 | 2.0480 | 19.9946 | 2.7750 |
| Vulkan | Standard | 2.3862 | 1.2008 | 2.9668 | 1.5399 |
| Vulkan | Stress | 17.3172 | 2.2331 | 19.2822 | 2.9403 |

05のStressでは、Vertex referenceからIndirectへ移すことでCPU記録の中央値がD3D12で0.2880→0.0081 ms、Vulkanで0.2732→0.0054 msとなりました。一方、Indirect内のカリングOFF→ONではGPU TotalがD3D12で2.1432→2.2159 ms、Vulkanで2.0950→2.2266 msと増えています。GPU DrivenによるCPU負担の削減とGPU全体の短縮は別に評価します。

06のStressではAS／TaskのONが両APIでOFFより短くなりました。StandardではVulkanの06・08・09でONが遅い結果もあり、小さい仕事量には判定コストが見えます。09のStressはAll→TiledでD3D12が17.9732→2.0480 ms、Vulkanが17.3172→2.2331 msでした。これは今回のGPU・ライト配置・カメラ経路の測定結果で、Mesh ShaderやTiledの一般的な速度保証ではありません。

## 記事用画像と範囲

試験を再実行する例です。GPU数値試験にはNumPyを利用できるPythonが必要です。既定のBlender付属Python以外を使う場合は`-PythonPath`で指定できます。

```powershell
Tools/TestMeshlets.ps1 -Configuration Debug
Tools/TestMeshlets.ps1 -Configuration Release
Tools/TestMeshShaderBinaries.ps1 -Configuration Debug
Tools/TestMeshShaderBinaries.ps1 -Configuration Release
Tools/TestMeshShaders.ps1 -Configuration Debug
Tools/TestMeshShaders.ps1 -Configuration Release -CaptureDocs
Tools/TestCameraNavigation.ps1 -Configuration Debug
Tools/TestMeshShaderFailures.ps1 -Configuration Debug
Tools/TestMeshShaderFailures.ps1 -Configuration Release
Tools/TestMeshShaders.ps1 -Configuration Release -BenchmarkOnly
```

新規PNGは52枚、すべて1280×720の実GPUキャプチャです。両APIのAlbedo、Standard／Stress、カリングOFF、Meshlet色分け、Shadow OFF、G-buffer法線、候補ライト数、容量超過とFallbackを目視確認しました。既存記事の画像は変更していません。

04はMeshletを導入せず、05はCompute＋Indirect、06以降はAS／Taskです。07〜09のShadowは方向光1本の2048×2048 MapとHard／3×3 PCFです。OPAQUE／MASKに限定し、BLEND、Hi-Z Occlusion Culling、LOD、CSM、点光源Shadow、Clustered Shadingへの切替は加えていません。DXR Part 2のClustered Geometryは共通章で形状分割のつながりを説明し、実装と必須機能へは追加していません。

検証結果と記事はこのリポジトリで管理します。Shader.jpの記事にも同じ実装、実GPU画像と測定データを使用します。
