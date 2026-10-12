# Tile-based Deferred／Clustered Shading 検証記録

2026年10月13日。第19回Tiled Deferredと第20回Clustered ShadingのDirect3D 12／Vulkan、追加4プロジェクトを検証しました。

## 検証環境

- Windows 11、NVIDIA GeForce RTX 4070、Windowsドライバ32.0.16.1088（610.88）。
- Visual Studio Community 2026 18.10.0、MSBuild 18.10.1、v145、MSVC 19.51.36257／`_MSC_FULL_VER=195136257`、C++20、Windows SDK 10.0.26100.0、x64。
- D3D12 Agility SDK 1.619.6、NuGet DXC 1.9.2609.5／01b62ad47。
- Vulkan SDK 1.4.341.1、付属DXC 1.10／1.9.0.5180／e35541826、Vulkan 1.3ターゲット。全SPIR-Vをspirv-valで検証。
- HLSL 2021、SM6.6、row-major、左手系、Depth 0〜1、Vulkanの負高さViewport。

## 対象と条件

G-bufferは4カラー＋Depth、18 B／画素を維持し、位置用ターゲットとクラスタID画像を追加しません。既存glTFの8／72点光源に、1区画あたり256個を追加します。Standard 264個、Stress 2,376個、最大設定のStressは4,680個です。追加ライトは固定Hash、4色、半径1.6 m、強度0.4。既存の無限rangeのライトを除外しません。

All／Tiled／Clusteredは同じ8×8のライティングShader、BRDF、ライト列を使います。TileDepthは16×16、CullLightsは64スレッド、Clusteredは対数Zの24スライスです。不透明Depthで有効セルを求め、透明物とForward診断では全ライトを評価します。容量超過は全ライトへ戻します。

## ビルド・CPU・ライフサイクル

追加4本のDebug／Releaseビルド、XML／filters、SPIR-V検証を通しました。ソリューションとfiltersへ項目を追加し、既存VS・SDK・ツールセットを維持しています。別工程で同時に更新されるAdvanced系プロジェクトも存在するため、検証対象を第1〜20回と区別しています。

旧`TestLoaders`、Workshopの共有形状・画像・配置、mip、接線、境界、座標変換、不正入力6種類の拒否を確認する`TestWorkshopLoader`が成功しました。

既存の第1〜18回、両APIの36実行サンプルでDebug／Releaseの描画、1280×720の撮影、640×480へリサイズ、最小化復帰、終了を確認しました。追加サンプルでは641×479の部分タイルと復帰、Standard／Stress／Full／室内カメラ、最大ライト数、動くライト、容量超過、診断表示を確認しました。Debug Layer／Validation Layerのエラーは0件です。

実際のImGuiスライダー入力でUI外の662,367画素が変化し、Reset後は基準画像へ一致しました。追加4本の両構成で、同じプロセスをTiled／Clustered／Allへ切り替えてからResetし、Bufferの再作成、Descriptor更新、同期、終了を確認しました。

## 数値とライトリスト

- 同一APIのAll／Tiled／Clusteredで、G0〜G3、Depth、Shadow Depthがバイト一致しました。
- Standard、Stress、641×479、Full、室内、4,680ライト、動くライト、容量1の条件で、HDR差が各成分の絶対`1e-3`＋相対`2e-3`以内でした。動的切替とResetでも同じ比較を通しました。
- 全HDR・Bloom画像のNaN／Infは0件です。
- 全セルのライトIDが範囲内で、同じリストに重複がありません。候補数と容量超過フラグが一致しました。容量1では全可視セルが容量超過し、全ライト評価の画を維持しました。
- GPUから読んだ各リストを、独立したCPUの球／平面判定で照合しました。確実に交差する有限ライトと、無限rangeのライトが欠落していません。
- タイルの最小／最大View Zを元のDepthからCPUで再計算して照合しました。空タイルの範囲とマスクは0で、出力にNaN／Infを含みません。

代表値はRelease、固定カメラで有効セルを均等に平均した候補数です。画素数で重み付けした平均ではありません。

| Scene／Camera | ライト数 | Tiled平均 | Clustered平均 | Tiled最大 | Clustered最大 |
|---|---:|---:|---:|---:|---:|
| Standard／Hero | 264 | 43.55 | 41.28 | 136 | 136 |
| Standard／Materials、半径3 m | 264 | 162.72 | 162.29 | 264 | 251 |

異なるAPIで境界の浮動小数点評価が変わる可能性があるため、API間の全Indexリストのビット一致は要求しません。リストへの登録順序による加算順の差もHDR許容誤差へ含めます。

## 画像とアセット

新規Docs画像16枚、各API8枚を実GPUから1280×720で撮影し、目視確認しました。Standardの完成表示・候補数・スライス・容量超過と、Stressの完成表示を掲載します。標準はHero／OpaqueMask、StressはStressHero、スライスはMaterialsです。UIは非表示、露出1、環境強度0.8、Shadow・PCF・IBL・Bloomを有効にしています。診断表示ではBloomとSkyboxの上書きを避けています。

既存331点のアセット／画像／正本ファイルがHEADと同じバイト列であることを確認しました。WorkshopのglTF・Blender正本を変更せず、ライトの追加はサンプルのCPU設定で行います。Cyclesの参考画像には追加ライトを反映していないため、既存条件との比較では追加数を0へ戻します。

## 再検証

```powershell
& Tools\ValidateProjects.ps1
& Tools\TestLoaders.ps1
& Tools\TestWorkshopLoader.ps1
& Tools\TestLightGrid.ps1 -Configuration Debug
& Tools\TestLightGrid.ps1 -Configuration Release -CaptureDocs
& Tools\TestLightGrid.ps1 -Configuration Release -BenchmarkOnly
```

Benchmarkは他のチュートリアルGPUプロセスが10秒間起動していないことを確認してから実行します。計測中に別プロセスが起動した実行は採用せず、再計測します。共有作業環境でGPU時間へ他のサンプルが混ざらないようにしています。


## Releaseの固定経路GPU時間

Release、1280×720、OpaqueMask、UI／VSyncなし。追加256個／区画、半径1.6 m、強度0.4、Shadow・PCF・IBL・Bloom有効。60 framesの準備後、同じ20秒の経路のframe 0〜1199を固定1/60秒で評価しました。各条件1200 framesです。リスト生成はTileDepth＋CullLights、TotalはShadowから表示処理までのGPU時間です。統計読戻しとCPU待機は含みません。

| API | Scene | Mode | リスト生成平均 ms | Lighting平均 ms | Total平均 ms | Total中央値 ms | Total p95 ms |
|---|---|---|---:|---:|---:|---:|---:|
| Direct3D12 | Standard | all | 0.000 | 1.629 | 2.202 | 2.266 | 3.104 |
| Direct3D12 | Standard | tile | 0.023 | 0.481 | 1.065 | 1.065 | 1.414 |
| Direct3D12 | Standard | cluster | 0.069 | 0.479 | 1.112 | 1.121 | 1.416 |
| Direct3D12 | Stress | all | 0.000 | 16.581 | 19.811 | 19.926 | 22.356 |
| Direct3D12 | Stress | tile | 0.064 | 0.897 | 4.110 | 3.966 | 5.264 |
| Direct3D12 | Stress | cluster | 0.111 | 0.885 | 4.133 | 4.007 | 5.231 |
| Vulkan | Standard | all | 0.000 | 1.624 | 2.300 | 2.342 | 3.225 |
| Vulkan | Standard | tile | 0.024 | 0.501 | 1.230 | 1.173 | 1.970 |
| Vulkan | Standard | cluster | 0.217 | 1.658 | 3.292 | 3.115 | 5.950 |
| Vulkan | Stress | all | 0.000 | 16.800 | 18.284 | 18.517 | 20.916 |
| Vulkan | Stress | tile | 0.066 | 0.940 | 2.455 | 2.324 | 3.752 |
| Vulkan | Stress | cluster | 0.199 | 2.074 | 4.475 | 4.417 | 7.249 |

この測定では、D3D12のStressで全体平均はAll 19.811 ms、Tiled 4.110 ms、Clustered 4.133 msでした。VulkanはTiledが両Sceneで短く、StandardのClusteredはAllより長くなりました。候補数が減っても、生成とメモリ参照、画素間の処理の違いにコストがあります。これらはこのGPU・Shader・シーン条件の結果で、Clusteredが常に速いという結論にはしません。

- Direct3D12 Standard all: [JSON](Measurements/LightGrid/Direct3D12-Standard-all.json)／[CSV](Measurements/LightGrid/Direct3D12-Standard-all.csv)
- Direct3D12 Standard tile: [JSON](Measurements/LightGrid/Direct3D12-Standard-tile.json)／[CSV](Measurements/LightGrid/Direct3D12-Standard-tile.csv)
- Direct3D12 Standard cluster: [JSON](Measurements/LightGrid/Direct3D12-Standard-cluster.json)／[CSV](Measurements/LightGrid/Direct3D12-Standard-cluster.csv)
- Direct3D12 Stress all: [JSON](Measurements/LightGrid/Direct3D12-Stress-all.json)／[CSV](Measurements/LightGrid/Direct3D12-Stress-all.csv)
- Direct3D12 Stress tile: [JSON](Measurements/LightGrid/Direct3D12-Stress-tile.json)／[CSV](Measurements/LightGrid/Direct3D12-Stress-tile.csv)
- Direct3D12 Stress cluster: [JSON](Measurements/LightGrid/Direct3D12-Stress-cluster.json)／[CSV](Measurements/LightGrid/Direct3D12-Stress-cluster.csv)
- Vulkan Standard all: [JSON](Measurements/LightGrid/Vulkan-Standard-all.json)／[CSV](Measurements/LightGrid/Vulkan-Standard-all.csv)
- Vulkan Standard tile: [JSON](Measurements/LightGrid/Vulkan-Standard-tile.json)／[CSV](Measurements/LightGrid/Vulkan-Standard-tile.csv)
- Vulkan Standard cluster: [JSON](Measurements/LightGrid/Vulkan-Standard-cluster.json)／[CSV](Measurements/LightGrid/Vulkan-Standard-cluster.csv)
- Vulkan Stress all: [JSON](Measurements/LightGrid/Vulkan-Stress-all.json)／[CSV](Measurements/LightGrid/Vulkan-Stress-all.csv)
- Vulkan Stress tile: [JSON](Measurements/LightGrid/Vulkan-Stress-tile.json)／[CSV](Measurements/LightGrid/Vulkan-Stress-tile.csv)
- Vulkan Stress cluster: [JSON](Measurements/LightGrid/Vulkan-Stress-cluster.json)／[CSV](Measurements/LightGrid/Vulkan-Stress-cluster.csv)
