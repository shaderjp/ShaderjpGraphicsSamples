# 第21回 Visibility Bufferの検証と性能比較

検証日：2026年10月15日（日本時間）。Direct3D 12／Vulkanの独立した第21回サンプルを、WorkshopのStandard／Stressで検証しました。性能値はこの機材・サンプル実装に対する実測です。

## 検証環境

- Windows 11 Pro 10.0.26300、NVIDIA GeForce RTX 4070、ドライバ610.88／32.0.16.1088。
- Visual Studio Community 2026 18.10.0、MSBuild 18.10.1、v145、MSVC 19.51.36257／`_MSC_FULL_VER=195136257`、C++20、x64、Windows SDK 10.0.26100.0。
- D3D12 Agility SDK 1.619.6、NuGet DXC 1.9.2609.5／01b62ad47。
- Vulkan SDK 1.4.341.1、付属DXC 1.10／1.9.0.5180／e35541826、Vulkan 1.3ターゲット。
- HLSL 2021、SM6.6、row-major、左手系、Depth 0〜1、Vulkanの負高さViewport。

## 保存形式と共有参照

可視性用の画像はRG32 UINTのIDとD32のDepthだけです。12 B／画素、1280×720で10.55 MiB。位置、法線、材質、重心座標のG-bufferはありません。HDRやShadow、Bloom、ライトリストは別途確保します。

固有形状を一つのVBとIBへ連結し、IAとComputeで同じGPU Bufferを参照します。Stressの配置ごとに頂点や画像をコピーしません。CPU／GPUの頂点48 B、配置144 B、材質80 Bを検証しました。Vulkanの頂点は3本のfloat4とし、SPIR-VのMember Offset 0／16／32とArray Stride 48を確認しています。float3／float2のstd430 Structによる64 Bのstrideとの差を避けています。

## ビルド・読込・操作

NuGet Restore、ソリューション全体のx64 Debug／Release、全62プロジェクトのXML／filtersと60 APIサンプルのソリューションフォルダを検証しました。VS・SDK・ツールセットを維持し、新規項目の未割当・重複・未宣言フィルタはありません。新規DXILをdxv、SPIR-Vをspirv-valで検証し、非一様な画像配列参照の宣言と共有Bufferの配置を確認しています。

既存の`TestLoaders`と`TestWorkshopLoader`も成功しました。OBJ／glTF／WIC／ファイル処理、Workshopの共有参照、材質、mip、接線、境界、座標変換と不正入力の拒否を確認しています。WorkshopのglTF、配布テクスチャ、環境、Blender正本を変更していません。

第21回の両API・Debug／ReleaseでStandard、Stress、Full、Near面、MASK／両面、透明合成、Forward診断、動く遮蔽物、黒入力、Tiled／Clustered／All、容量超過と診断表示を確認しました。641×479と1280×720への復帰、リサイズ、最小化復帰、終了、マウス回転、WASD／Q/E、カメラResetを確認しました。Debug LayerとVulkanのSynchronization Validationの検証エラーは0件です。

## 数値検証

- 背景のIDは0、IDがある画素とDepthが1未満の画素の集合が一致します。配置・三角形・Index・頂点・材質の参照が全て範囲内です。
- 裏面bitを持つIDが指す材質は両面設定です。BLENDはIDパスから除外し、Forwardで合成します。
- 同じAPIのTiled／All／Clustered／容量1でID、Depth、Shadow Depthがバイト一致し、HDRは各成分の絶対`1e-3`＋相対`2e-3`以内です。ライトIDの範囲、重複、候補数と容量超過の対応、無限rangeのライトが残ることを確認しました。
- Standardの可視332,840画素から12,328画素を抽出し、CPUの独立した連立方程式で重心座標を照合しました。Shaderと同じ外積式をテスト側へコピーしていません。
- UV勾配はCPUの両側有限差分で12,328画素を照合しました。Nearを1 mにした室内カメラでも12,021画素の重心座標と投影Depthを確認しました。重みの和、重心座標、勾配の99.9パーセンタイル誤差と最大誤差を検査しています。
- 全HDR／Bloom画像のNaN／Infは0件で、黒入力は黒を維持します。遮蔽物をXへ1 m動かすと、両APIでShadow Depthの5,484画素が変わります。
- カメラReset後は定数・配置・材質・ライト、IDとDepthがバイト一致し、HDR差は絶対`1e-3`＋相対`2e-3`以内です。ライトの登録・加算順序の差により、Debugの表示画像では1画素に1階調の差を観測しました。画像だけを一致条件にせず、実際のGPUデータとCPU状態でResetを確認します。

第19・20回とのDepthとShadow Depthはバイト一致します。一方、材質はG-bufferの8-bit／SNORMなどの量子化を経由せず、テクスチャのmip選択も解析的微分を使います。HDRのビット一致を条件にはしません。以下はRelease・Standard／HeroのRGB比較です。正規化差は`abs(A−B) / (0.02 + max(A,B))`と定義します。

| API／参照 | HDR絶対差の平均 | 最大 | 正規化差p99 |
|---|---:|---:|---:|
| Direct3D12／Forward | 0.00011685 | 16.250 | 0.000908 |
| Direct3D12／第19回 | 0.00091769 | 31.750 | 0.007545 |
| Direct3D12／第20回 | 0.00091769 | 31.750 | 0.007545 |
| Vulkan／Forward | 0.00011998 | 12.250 | 0.000913 |
| Vulkan／第19回 | 0.00091372 | 28.750 | 0.007544 |
| Vulkan／第20回 | 0.00091372 | 28.750 | 0.007544 |

強いハイライトでは局所的に大きな絶対差があります。平均差だけで全画素が一致すると判断しないでください。数値と、1280×720の完成表示・三角形・UV勾配・透明表示を目視確認しています。Cyclesとの画素一致を要求する検証ではありません。

## 第19・20回とのGPU性能比較

古い記事の測定を転記せず、第19・20・21回を今回のビルドで再計測しました。第21回の既定はTiledで、第19回と同じライトリスト生成・BRDFを使います。第20回との差にClusteredのZ分割が含まれるため、第21回をClusteredへ切り替えた結果も用意しました。

Release、1280×720、OpaqueMask、UI非表示、VSyncなし。露出1、環境強度0.8、Shadow・3×3 PCF・IBL・Bloom有効、固定Hash・半径1.6 m・強度0.4の追加ライト256個／区画です。Standardは264点光源、Stressは2,376点光源。60フレーム準備後、同じ20秒の経路を固定1/60秒、1200フレームで評価します。他のサンプルとゲームを同時実行せず、各条件の開始前にGPU使用率を3回確認しました。最大値は4〜17%で、全条件が待機基準の20%以下です。GPU完了後にTimestampを読みます。

全体平均GPU時間は次のとおりです。

| API／Scene | 第19回 Tiled G-buffer | 第20回 Clustered G-buffer | 第21回 Tiled Visibility | 第21回 Clustered Visibility |
|---|---:|---:|---:|---:|
| Direct3D12／Standard | 1.108 ms | 1.136 ms | 1.002 ms | 1.074 ms |
| Direct3D12／Stress | 4.234 ms | 4.166 ms | 3.848 ms | 3.880 ms |
| Vulkan／Standard | 1.205 ms | 4.619 ms | 0.967 ms | 3.244 ms |
| Vulkan／Stress | 2.356 ms | 5.297 ms | 2.067 ms | 5.475 ms |


この測定では、Tiledの第19回から第21回への全体時間は、Direct3D 12でStandard約9.5%・Stress約9.1%、VulkanでStandard約19.8%・Stress約12.3%短くなりました。各条件でIDパスはG-buffer構築より短く、属性復元を加えたlightingは長くなっています。

Clustered同士では、VulkanのStressが第20回5.297 msに対して第21回5.475 msで、約3.4%長くなりました。Standardでは4.619 msから3.244 msへ短縮しています。候補の絞込みだけでなく、セル管理・形状参照・材質評価と実装全体の時間を見る必要があります。単一機材のこの測定から、VisibilityやClusteredの優劣を一般化しません。

詳細は各パスの平均と全体の中央値・p95です。単位はms。`geometry`は第19・20回のG-buffer構築と第21回のID構築、`lighting`は第21回では属性復元と材質評価も含みます。

| API／Scene／方式 | geometry平均 | tile-depth平均 | cull平均 | lighting平均 | 全体平均 | 全体中央値 | 全体p95 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Direct3D12／Standard／19 Tiled | 0.248 | 0.011 | 0.012 | 0.491 | 1.108 | 1.096 | 1.598 |
| Direct3D12／Standard／20 Clustered | 0.247 | 0.011 | 0.058 | 0.479 | 1.136 | 1.129 | 1.549 |
| Direct3D12／Standard／21 Tiled | 0.181 | 0.012 | 0.012 | 0.510 | 1.002 | 0.995 | 1.399 |
| Direct3D12／Standard／21 Clustered | 0.192 | 0.011 | 0.058 | 0.513 | 1.074 | 1.059 | 1.517 |
| Direct3D12／Stress／19 Tiled | 1.336 | 0.012 | 0.050 | 0.887 | 4.234 | 4.102 | 5.301 |
| Direct3D12／Stress／20 Clustered | 1.294 | 0.012 | 0.099 | 0.874 | 4.166 | 4.041 | 5.196 |
| Direct3D12／Stress／21 Tiled | 1.257 | 0.012 | 0.053 | 0.931 | 3.848 | 3.766 | 4.832 |
| Direct3D12／Stress／21 Clustered | 1.271 | 0.011 | 0.100 | 0.915 | 3.880 | 3.786 | 4.990 |
| Vulkan／Standard／19 Tiled | 0.214 | 0.013 | 0.012 | 0.485 | 1.205 | 1.173 | 1.824 |
| Vulkan／Standard／20 Clustered | 1.470 | 0.034 | 0.183 | 1.732 | 4.619 | 4.494 | 7.298 |
| Vulkan／Standard／21 Tiled | 0.072 | 0.013 | 0.015 | 0.534 | 0.967 | 0.968 | 1.263 |
| Vulkan／Standard／21 Clustered | 0.295 | 0.033 | 0.177 | 1.768 | 3.244 | 3.351 | 6.238 |
| Vulkan／Stress／19 Tiled | 0.568 | 0.012 | 0.051 | 0.903 | 2.356 | 2.267 | 3.510 |
| Vulkan／Stress／20 Clustered | 1.948 | 0.020 | 0.157 | 1.648 | 5.297 | 5.081 | 8.414 |
| Vulkan／Stress／21 Tiled | 0.415 | 0.013 | 0.053 | 0.957 | 2.067 | 1.991 | 2.973 |
| Vulkan／Stress／21 Clustered | 0.907 | 0.024 | 0.216 | 2.540 | 5.475 | 5.311 | 7.674 |


全体にはShadow、Skybox、透明パス、Bloom、表示処理も含まれます。CPUの更新・APIコマンド記録・Present待機はGPU Timestampの値に含みません。VB／IBは従来と同じUpload Heap／HOST_VISIBLEで、Heap／Memory Typeを変える最適化、材質のWorklist、Mesh Shaderは加えません。これらを変えた場合の性能を、この結果から断定できません。

第21回は元のVB／IBをComputeでも参照するため、固有形状を共有Bufferへ連結しています。従来のPrimitiveごとのResourceとは割当単位も異なります。表はその実装全体の比較であり、ID画像の容量削減だけの効果を分離した実験ではありません。GPU周波数は固定しておらず、単一機材・各条件1回の1200フレーム測定です。

Clusteredの固定長リストは約85.03 MiB、Tiledは約3.54 MiBです。無効セルのHeader更新と全セルへのDispatchは残ります。Visibilityの画像容量削減と、ライト選択の管理コストを混同しないでください。

測定CSV／JSON、平均・中央値・p95、GPU・コンパイラ、ライト数、候補数と容量は[Docs/Measurements/Visibility](Measurements/Visibility/)に保存しています。CSVは各条件1200行で、[Comparison.json](Measurements/Visibility/Comparison.json)には同じカリング方式のG-bufferとVisibilityの全体時間比を記録します。

## 再検証

```powershell
& Tools\ValidateProjects.ps1
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild ShaderjpGraphicsSamples.sln -m -p:Configuration=Debug -p:Platform=x64
msbuild ShaderjpGraphicsSamples.sln -m -p:Configuration=Release -p:Platform=x64
& Tools\TestLoaders.ps1
& Tools\TestWorkshopLoader.ps1
& Tools\TestVisibilityShaders.ps1 -Configuration Debug
& Tools\TestVisibilityShaders.ps1 -Configuration Release
& Tools\TestVisibility.ps1 -Configuration Debug
& Tools\TestVisibility.ps1 -Configuration Release -CaptureDocs
& Tools\TestSamples.ps1 -Configuration Debug -Topics 21_VisibilityBuffer
& Tools\TestSamples.ps1 -Configuration Release -Topics 21_VisibilityBuffer
& Tools\TestCameraNavigation.ps1 -Configuration Debug -Topics 21_VisibilityBuffer
& Tools\TestCameraNavigation.ps1 -Configuration Release -Topics 21_VisibilityBuffer
& Tools\TestVisibility.ps1 -Configuration Release -BenchmarkOnly
```

新規記事画像は実GPUから1280×720で撮影した両API各7枚です。既存の800×600画像と、既存の第19・20回の画像は維持しています。
