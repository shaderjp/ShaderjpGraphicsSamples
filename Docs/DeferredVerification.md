# Deferred Rendering 検証記録

2026年10月12日、Windows 11、NVIDIA GeForce RTX 4070で検証しました。第17回PS・第18回CSをDirect3D 12／Vulkanで実装した4本が対象です。

## 実行環境

- Visual Studio Community 2026 18.10.0、MSBuild 18.10.1、v145、MSVCコンパイラ19.51.36257（ツールセットディレクトリ14.51.36231）、Windows SDK 10.0.26100.0、C++20、x64。
- D3D12 Agility SDK 1.619.6、NuGet DXC 1.9.2609.5／01b62ad47。
- Vulkan SDK 1.4.341.1、付属DXC 1.10／1.9.0.5180／e35541826、Vulkan 1.3ターゲット。ビルド時にspirv-valで検証。
- GPU: NVIDIA GeForce RTX 4070、Windowsドライバ32.0.16.1088（610.88）。
- HLSL 2021、SM6.6、row-major、左手系、Depth 0〜1。Vulkanは負高さViewport。

## ビルド・CPU・描画

`ValidateProjects`で37プロジェクトとfiltersを検証し、Restoreと全プロジェクトのDebug／Releaseビルドが成功しました。SDK・ツールセット・Submoduleの版を維持しています。

旧`TestLoaders`が成功しました。追加`TestWorkshopLoader`では62 Primitive、16 Material、40 Image、Standardの201 Primitive配置／251,200三角形／点光源8個、Stressの1,809配置／2,260,800三角形／72個を確認しました。共有する固有データをシーンや配置ごとに複製しません。これらはFullの数値です。

法線の極・折返しと10万方向のSNORM16往復を確認し、最大誤差は0.00364395度でした。奇数サイズのmip、線形空間の色平均、Normalの再正規化、全40画像のmip生成が成功しました。階層循環、ID重複、未対応Extension、過大Accessor、NaN頂点、範囲外Indexの6種類を拒否しました。

全36実行サンプルのDebug／Releaseで、1280×720、640×480へのリサイズ、最小化復帰、撮影、終了を確認しました。新規4本はStandard／Stress／Full／Forward、641×479、UI操作・Reset、Shadowなし／硬い影／PCF、遮蔽物移動、MASK、Bloom、黒入力、一定色入力、全診断表示を追加試験しました。Debug Layer／Validation Layerのエラーは0件です。

GBufferPixelと、各版のLightingPixel／LightingComputeを隔離した配布フォルダから欠落させ、両API・両構成でファイル名を含むエラー、終了コード1、部分初期化の解放、検証エラー0件を確認しました。

## 数値比較

| 条件 | 結果 |
|---|---|
| 同一APIのPS／CS、Standard・Stress・Full・奇数サイズ | G0〜G3・Depth・Shadow Mapがバイト一致 |
| D3D12 Debug HDR最大差 | Standard 0.00048828125、Stress 0.0009765625、Full 0.00048828125、奇数サイズ0.000244140625 |
| D3D12 Release・Vulkan Debug／Release HDR | PS／CSで一致 |
| HDR許容誤差 | 全条件で絶対1e-3＋相対2e-3以内 |
| Forward比較 | Depth一致、表示RGBの最大差3／255。材質量子化と再構築の差を含む |
| 深度再構築 | 独立したCPUレイ／三角形交差7点に一致、最大位置誤差0.00024054266 m |
| HDR発光・Bloom画像 | HDR値を保持、NaN／Infなし。黒入力は抽出・Blur後もRGBが0 |
| UI／Reset | 実スライダー入力で照明が変化し、Reset後のUI外のシーン画素が基準へ一致 |

G-bufferは14 B／画素のカラーと4 B／画素のDepth、合計18 B／画素です。位置用ターゲットと線形Depthの複製はありません。CPUのレイ試験は配布glTFの形状とカメラ条件を使い、GPUから位置を読み戻していません。

## ReleaseのGPU時間

1280×720、OpaqueMask、UIなし、VSyncなし、Shadow・3×3 PCF・IBL・Bloom有効、露出1、環境強度0.8、Bloom Threshold 1／Strength 0.15。60 framesの準備後、同じCamera Pathのframe 0〜1199を固定1/60秒で評価しました。各条件1200 framesです。

LightingはG-buffer読込・位置再構築・全点光源のループ・IBL・発光を含みます。TotalはShadowからDisplayまでのGPU時間で、CPU待機とPresent待機を含みません。Geometryは個別Drawで、GPUインスタンシングやTiled／Clusteredを使いません。

| API | Scene | 版 | Lighting平均 ms | Total平均 ms | Total中央値 ms | Total p95 ms |
|---|---|---|---:|---:|---:|---:|
| Direct3D12 | Standard | Pixel | 0.179 | 1.351 | 0.787 | 7.503 |
| Direct3D12 | Stress | Pixel | 0.986 | 4.785 | 4.387 | 6.745 |
| Direct3D12 | Standard | Compute | 0.196 | 0.882 | 0.853 | 1.043 |
| Direct3D12 | Stress | Compute | 1.117 | 4.852 | 4.274 | 7.324 |
| Vulkan | Standard | Pixel | 0.174 | 1.179 | 0.935 | 1.590 |
| Vulkan | Stress | Pixel | 1.128 | 5.819 | 2.801 | 13.523 |
| Vulkan | Standard | Compute | 0.223 | 1.589 | 0.893 | 6.874 |
| Vulkan | Stress | Compute | 1.817 | 6.081 | 2.816 | 13.867 |

今回のLighting平均では両APIともPSがCSより短くなりました。Totalの分布には変動があり、この一回の測定から一般的な速度の優劣は結論しません。後続のライト絞り込みでは同じ条件で再計測します。

JSONにはGPU・構成・コンパイラ・SDK・ドライバ・ライト数・計測条件と、全パスの平均・中央値・p95を保存しています。CSVには1200 framesの各パス時間を保存しています。

- Direct3D12 Standard Pixel: [JSON](Measurements/Deferred/Direct3D12_17_DeferredPixel-Standard-timings.json)・[CSV](Measurements/Deferred/Direct3D12_17_DeferredPixel-Standard-timings.csv)
- Direct3D12 Stress Pixel: [JSON](Measurements/Deferred/Direct3D12_17_DeferredPixel-Stress-timings.json)・[CSV](Measurements/Deferred/Direct3D12_17_DeferredPixel-Stress-timings.csv)
- Direct3D12 Standard Compute: [JSON](Measurements/Deferred/Direct3D12_18_DeferredCompute-Standard-timings.json)・[CSV](Measurements/Deferred/Direct3D12_18_DeferredCompute-Standard-timings.csv)
- Direct3D12 Stress Compute: [JSON](Measurements/Deferred/Direct3D12_18_DeferredCompute-Stress-timings.json)・[CSV](Measurements/Deferred/Direct3D12_18_DeferredCompute-Stress-timings.csv)
- Vulkan Standard Pixel: [JSON](Measurements/Deferred/Vulkan_17_DeferredPixel-Standard-timings.json)・[CSV](Measurements/Deferred/Vulkan_17_DeferredPixel-Standard-timings.csv)
- Vulkan Stress Pixel: [JSON](Measurements/Deferred/Vulkan_17_DeferredPixel-Stress-timings.json)・[CSV](Measurements/Deferred/Vulkan_17_DeferredPixel-Stress-timings.csv)
- Vulkan Standard Compute: [JSON](Measurements/Deferred/Vulkan_18_DeferredCompute-Standard-timings.json)・[CSV](Measurements/Deferred/Vulkan_18_DeferredCompute-Standard-timings.csv)
- Vulkan Stress Compute: [JSON](Measurements/Deferred/Vulkan_18_DeferredCompute-Stress-timings.json)・[CSV](Measurements/Deferred/Vulkan_18_DeferredCompute-Stress-timings.csv)

## 画像と比較条件

新規Docs画像は両API各9枚、全18枚が1280×720です。全景PS／CS、World Normal、Depth、材質接写、透明カバー、Stress、Bloom抽出、Shadow Mapを実GPUから撮影し、目視確認しました。標準はStandard／Hero／OpaqueMask、透明はFull／Transparency、負荷版はStress／StressHeroです。Skyboxと透明を加えた線形HDRへBloomを適用し、表示変換を最後に一度だけ行います。Shadow Map診断は画面全体へ表示します。

Cyclesと比較するときは、点光源Shadow、間接光、IBL近似、材質モデル、Alpha合成、AgXによる差を区別します。[共通解説](DeferredRendering.md)と[Workshop参考画像](Assets/Workshop.md)を参照してください。

サイト記事リポジトリへの変更・公開・pushはこの工程に含めません。
