# 発展20〜23 Clustered Geometry／Partitioned TLASの検証

検証日：2026-10-30（日本時間）。[共通解説](ClusteredRaytracing.md)に対応するローカル実機検証です。Direct3D12に4本を追加し、全体は102 APIアプリ（D3D12 53／Vulkan 49）と3 Supportプロジェクトです。

## 環境

| 項目 | 使用した版 |
|---|---|
| OS／CPU | Windows 11 Pro 10.0.26300／Core i9-13900F |
| GPU／ドライバ | GeForce RTX 4070／621.01（32.0.16.2101） |
| Visual Studio | 2026 Community 18.10.12201.205 |
| C++ | v145、MSVC Tools 14.51.36231、cl 19.51.36257.0、C++20 |
| Windows SDK | 10.0.26100.0 |
| 既存D3D12 | Agility 1.620.1／SDK export 620／DXC 1.9.2609.5（01b62ad47） |
| 新4本 | Agility 1.722.0-preview／SDK export 722／DXC 1.10.2605.37-preview（c4d8f4f99） |
| シェーダ | HLSL 2021、20〜22のCluster LibraryだけSM6.10、ほかはSM6.6 |
| Vulkan回帰 | SDK 1.4.341.1、従来のVulkan 1.3ターゲット |
| アセット生成 | Blender 5.2.1 LTS、固定seed 20261029 |

Developer Modeは有効です。実機の`CheckFeatureSupport1`で`ClustersAndPTLASSupported=1`と必要なシェーダモデルを確認し、MicrosoftのPreview APIでCLAS・テンプレート・PTLASを実行しました。[環境・コンパイラ・配布DLL・バイナリのハッシュ](ClusterGeometryEnvironment.json)を保存しています。

既存D3D12も`ID3D12Device16::CheckFeatureSupport1`へ移行しました。Agility 620で従来の照会APIから出る非推奨警告を解消し、選択したSDKバージョンを照会へ渡しています。既存のシェーダモデル、Windows SDK、toolsetは維持しています。

## ビルドと回帰

- NuGet Restore、全105プロジェクトのx64 Debug／Releaseビルドに成功しました。VulkanのSPIR-V検証も通過しています。
- D3D12の53アプリ×2構成について、配布先のCore／Debug Layer DLLが選択したNuGetパッケージと同じハッシュであることを確認しました。
- `ValidateProjects.ps1`で全vcxproj／filtersのXML、全項目の一意なfilter割当、未宣言filter、GUID、102アプリのソリューション登録を検査しました。
- 全102アプリを両構成で起動し、描画・撮影・リサイズ・最小化復帰・終了の計204ケースを確認しました。新4本は1920×1080へ復帰します。
- Loader、ImGui、Raytracing、Bindless、PathTracingの既存専用回帰を両構成・両APIで実行しました。Inline RTは`-FixturesOnly`、BSDFは`-Quick`で関連回帰を実行しました。
- DebugのD3D12 Debug Layer／Vulkan Validation Layerは有効、グラフィックス検証エラーは0件です。D3D12の警告も検査しています。

[全体結果と画像ハッシュ](ClusterGeometryResults.json)／[ビルド・検証ログ](Measurements/ClusterGeometry/ValidationLogs.zip)

## 新4本の専用試験

`TestClusterGeometry.ps1`は1構成32ケース、両構成64ケースです。通常の数値比較は640×360で行い、ライフサイクル試験と記事用画像は1920×1080です。

| 対象 | 確認内容 |
|---|---|
| 20 | CLAS直接構築／通常BLAS、既知の平面への距離、高密度実演モデルのStress |
| 21 | テンプレート／直接CLAS／通常BLAS rebuild／refit、実演用の傾いた薄板、生成位置・法線・接線 |
| 22 | 自動LOD、固定2／4／8、選択メタデータ、全33境界トポロジー、変位と法線 |
| 23 | PTLAS／通常TLAS rebuild／refit、spatial／global、一部・0件・全件更新、Stress 16,385インスタンス |
| UI | 21〜23の実際のStep／Resetクリック、時間とHDRの変化、Reset後の復元 |
| CPU | 全頂点属性を含む三角形の欠落・重複、クラスタ上限、境界の面積・向き・頂点、PTLAS最大容量 |
| 異常系 | 両構成それぞれ21件。機能未対応分岐、未定義の方式・表示、無効LOD・partition、不正なアセット範囲を非0終了で拒否 |
| シェーダ | Cluster LibraryのSM6.10と`ClusterID`、通常Library／ComputeのSM6.6、23でクラスタLibraryを使用しないこと |

GPU生成データは独立したCPU式と比較し、位置・法線・接線の許容値は絶対／相対とも2e-5です。クラスタメタデータは完全一致を要求します。ヒットIDは1ピクセルの幾何境界を除いて99.9%超、ヒット距離は相対5e-5＋絶対1e-4 m以内を要求します。

既知の平面への距離は倍精度のカメラ式でも計算します。GPU側はfloat32の逆View Projection行列を使うため、相対3e-5＋絶対1e-4 mで検査します。今回の最大差は約0.4534 mmでした。

線形HDRは、テクスチャ・影・反射の急変箇所から1ピクセル離れた画素で、絶対0.002＋相対0.002以内の一致率99.5%超を要求します。幾何境界以外の全画素にも正規化RMS 2.5%未満を要求します。CLASと通常三角形の交差精度による境界差を許容しつつ、広い領域の不一致を検出する条件です。

[Debug数値結果](ClusterGeometryGpuDebug.json)／[Release数値結果](ClusterGeometryGpuRelease.json)。GPU読戻しは試験専用で、通常のLODやAS構築の制御には使いません。

アセットの再生成で実行用バイナリとJSONが同じバイト列になることも確認しました。[Manifest](../Assets/Models/GeometryLab/Manifest.json)に実行データ、生成スクリプト、Blender正本のハッシュを記録しています。meshoptimizer v1.3とそのvendorソースは変更していません。

## Release計測の読み方

[集計表](Measurements/ClusterGeometry/Summary.md)／[中央値・p95・各試行の中央値](Measurements/ClusterGeometry/Summary.json)。同じフォルダに65試行分のフレーム別JSONとログを保存しています。

Standard、1920×1080、UI非表示、固定カメラ、開始時刻0.7秒、固定時間刻み1/60秒です。13条件それぞれ120フレームのウォームアップ後、600フレームを5回測定します。同じ技法内でインスタンス数、形状、BLAS共有、アニメーション時刻を照合します。ほかのサンプルと実行が重なった試行は取り直します。

表は各試行の中央値の中央値、p95は5試行の計3,000測定フレームから求めます。GPU timestampの区間時間であり、CPUの構築準備、待機、Presentを含むアプリ全体のフレーム時間ではありません。20は静的ASも毎フレーム再構築して構築費を測り、23の静的BLAS初期構築はフレームごとのトップレベル更新時間から除外します。

今回の中央値では、20のCLAS生成0.127 ms＋Cluster BLAS 0.101 msに対し、通常BLAS rebuildは0.541 msでした。21はテンプレート実体化0.015 ms、直接CLAS生成0.075 msです。一方、21のtraceはテンプレート経路0.779 ms、通常BLAS経路0.656 msでした。

23のトップレベル更新はspatial PTLAS 0.119 ms、通常TLAS rebuild 0.119 ms、refit 0.016 msです。この条件ではrefitが軽く、PTLASによる高速化は確認していません。22でもテンプレートを含むAS容量0.46 MiBは通常BLASの0.12 MiBより大きくなっています。構築時間、trace時間、保持領域を分けて比較する必要があります。

`initialBuildTimesMs`は最初に観測した各GPU区間の値です。配列順は頂点／インスタンス生成、CLAS、BLAS、TLAS／PTLAS、trace、displayです。起動処理全体の時間や、テンプレート事前構築の合計時間を表すものではありません。

メモリはAS result、scratch、入力を分けたBuffer容量です。`source`には構築・形状生成用バッファと状態、保持している元データのコピーを含めます。`geometry`は頂点とインデックス1組だけの大きさです。材質・IBL・表示資源や、配置アラインメントを含む物理VRAM使用量は集計していません。

速度向上は合否条件にしていません。今回の小規模な常駐モデルと共有方針での比較であり、他GPU・他ドライバや大規模ストリーミングへそのまま一般化できません。機能未対応の実GPUは手元にないため、その終了分岐は明示的な試験用フラグでも確認しています。

## 再実行

```powershell
pwsh Tools/ValidateProjects.ps1
pwsh Tools/TestLoaders.ps1
pwsh Tools/TestClusterGeometryCpu.ps1 -Configuration Debug
pwsh Tools/TestClusterGeometryCpu.ps1 -Configuration Release
pwsh Tools/TestClusterGeometry.ps1 -Configuration Debug
pwsh Tools/TestClusterGeometry.ps1 -Configuration Release
pwsh Tools/TestClusterGeometryShaders.ps1 -Configuration Debug
pwsh Tools/TestClusterGeometryFailures.ps1 -Configuration Debug
pwsh Tools/CaptureClusterGeometryDocs.ps1
pwsh Tools/MeasureClusterGeometry.ps1
pwsh Tools/RecordClusterGeometryEnvironment.ps1
```

記事用の実機画像11枚はすべて1920×1080で取得して目視確認しました。既存画像と過去の検証記録は保持しています。この記録は公開前に行ったローカル実機検証の結果です。
