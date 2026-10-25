# 発展19 BRDFとBSDFの検証記録

検証日：2026-10-25（日本時間）。`Advanced19_BSDFMaterials`のDirect3D12／Vulkanを対象とします。[共通解説](BRDFAndBSDF.md)／[共通アセット](Assets/BSDFMaterials.md)／[環境JSON](BSDFMaterialsEnvironment.json)／[数値と撮影条件](BSDFMaterialsResults.json)

## 実測環境

| 項目 | 使用したバージョン |
|---|---|
| OS／CPU | Windows 11 Pro 10.0.26300.0／Intel Core i9-13900F |
| GPU／ドライバ | GeForce RTX 4070／NVIDIA 621.01（32.0.16.2101） |
| Visual Studio | 2026 Community 18.10.12201.205、MSBuild 18.10.1 |
| C++／Windows SDK | v145、cl 19.51.36257.0、C++20、10.0.26100.0 |
| D3D12 | Agility SDK 1.619.6、DXC 1.9.2609.5（01b62ad47） |
| Vulkan | SDK 1.4.341.1、DXC 1.9.0.5180（e35541826、DLL表示1.10） |
| シェーダー | SM6.6、HLSL 2021、row-major、Vulkanターゲット1.3 |
| SPIR-V検証 | SPIRV-Tools v2026.1（fbe4f3ad） |
| アセット制作／検証 | Blender 5.2.1 LTS（9e2066aef7ef）、glTF-Validator 2.0.0-dev.3.10 |

## ビルドとCPUデータ

Restoreとソリューション全101プロジェクト（APIサンプル98本＋Support 3本）のx64 Debug／Releaseビルドを通過しました。最終ビルドの警告・エラーは0件です。VS、SDK、toolsetの変更は行っていません。

- `Tools/ValidateProjects.ps1`で全project／filtersをXMLとして検査し、項目の一対一対応、GUID、親フィルタ、ファイル存在、ソリューション配置を確認しました。
- `Tools/TestBSDFShaders.ps1`は両構成で成功しました。DXIL／SPIR-V、KHR機能、RGBA32F、Vertex 48／Instance 144／Material 112／AreaLight 64 bytes、Scene定数256 bytesをCPUのstatic_assertと照合します。Materialの追加offsetは80／96、Scene末尾は240です。
- `Tools/TestBSDFCpu.ps1`はRoomの安定IDによる合成、ガラス物性、内部／外部カメラ、非一様スケール、吸収係数を確認します。不正IOR、部分Transmission、薄膜、BLEND、透過・厚みテクスチャ、未対応拡張、開メッシュ、体積の重なりを拒否しました。
- `Tools/TestBSDFAssets.ps1`はglTFのエラー・警告・情報・ヒントが全て0件でした。保存済みblendからの隔離した再書出し、正本のハッシュ不変、Index／法線／接線／UV、閉メッシュと外向き、安定ID、参照とManifestを確認しました。ノード変換・物性を含むglTF JSON、カメラ・PresetのScene JSON、PNGのバイト列も配布物と一致しました。
- `core.autocrlf=true`の隔離チェックアウトでも、Manifestの配布物・制作正本・流用元ハッシュが全て一致しました。旧Roomの正本・配布物と発展18のHLSLは変更していません。

## GPUからの数値照合

両API・両構成で、光学・吸収・統計・UI・カメラ・ライフサイクルの専用GPU試験を通過しました。終了コード、Validation errors: 0、Debugの検証レイヤー有効化を確認し、Vulkan Debugは同期検証も有効です。

`Tests/BSDFGpuTests.py`はHLSLを呼び出すCPUコピーではなく、float64の光学式と方向積分から参照を作ります。GPUの最終サンプル、HDR平均、交差診断を別々に読み戻します。

- IOR 1.5の垂直入射Fresnelは0.04、30度入射の屈折方向は`(-1/3, 0, -sqrt(8/9))`です。角度別の入射・出射Fresnel、50度での全反射を照合しました。
- 滑らかな境界での反射／透過の選択頻度、入射時`1/2.25`・出射時`2.25`のradiance重み、同じIORの境界がそのまま通過することを確認しました。反射成分だけの場合は、垂直入射の重みが0.04であり、1へ増やしません。
- 粗さ0.3の入射・出射、正面・斜めと従来の不透明PBRについて、評価値・方向PDF・throughputを相対0.004＋絶対`3e-5`以内で比較します。
- 別の推定器で球面全体から262,144方向を一様に選び、PDF積分とGPUサンプラの受理率・反射選択率、`f * abs(N・L)`の積分と平均重みを比較しました。許容差は双方の推定分散の5標準誤差＋0.003です。
- 吸収色`(0.18, 0.65, 0.85)`、距離0.5 mに対し、既知の0.5／2 mの減衰を照合しました。屈折率1の板では、実際の交差間距離`d`と最終サンプルの`C^(d/0.5)`が相対0.001＋絶対`2e-4`以内で一致しました。薄い板への非一様スケールでも同じ照合を行います。診断は最後のジッター経路なので、時間平均画像と直接は比較しません。
- 白い環境中の透明球はseed 1／7／19、各256 sppで球に当たった画素だけを比較し、RGB平均が1から0.003＋5標準誤差以内です。これは滑らかなガラスの試験で、粗い微小面の多重散乱補償を検証するものではありません。
- すりガラスのROIは`x=1120..1449, y=360..649`、3 seed各128 sppでMIS／BSDFと高spp参照を比較します。MIS／BSDF間は絶対0.002＋5標準誤差、高spp参照へはその2倍を許容します。専用コースティクス手法の評価ではありません。
- 拡散床の`x=900..1199, y=960..1019`で、1／16／256 sppの高spp参照に対するMSEが減少することを確認します。Direct3D12 Releaseでは順に`0.1532844 / 0.00877017 / 0.000419315`でした。seed 1の入れ子のサンプル列であり、一般的な性能・収束率を保証する数値ではありません。
- 黒い環境、内部カメラの開始媒質、透過後の空気への復帰、HDR／診断のNaN・Inf不在、同じseed・sppの再現性を確認します。

すりガラスROIのRGB平均です。MIS／BSDFは3 seed各128 sppの平均、参照はDebugが256 spp、Releaseが1024 sppです。白い環境中の透明球の平均は全4条件で約0.999528〜0.999529でした。画像のAPI間バイト一致を合否条件にはしません。

| 構成／API | MIS | BSDF | 参照 |
|---|---|---|---|
| Debug / Direct3D12 | 0.104986 / 0.205906 / 0.103547 | 0.105154 / 0.206055 / 0.103717 | 0.104959 / 0.205897 / 0.103561 |
| Debug / Vulkan | 0.104982 / 0.205898 / 0.103549 | 0.105155 / 0.206063 / 0.103723 | 0.104962 / 0.205903 / 0.103558 |
| Release / Direct3D12 | 0.104987 / 0.205903 / 0.103543 | 0.105154 / 0.206054 / 0.103715 | 0.105025 / 0.205994 / 0.103601 |
| Release / Vulkan | 0.104982 / 0.205895 / 0.103544 | 0.105155 / 0.206067 / 0.103725 | 0.105025 / 0.205992 / 0.103601 |

## 操作・ウィンドウ・回帰

実際のImGuiイベントでIORを編集し、Reset後は同じ4 sppの線形HDRへ完全に戻ります。3モードの往復も同じ条件へ戻ります。露出変更では線形HDRを維持し、表示だけを変えます。蓄積上限なしでPause中のHDR／sppが不変で、Resume後にsppが増えることを別試験で確認します。

CLIで反射成分のみ、Thickness、ThicknessObliqueカメラ、吸収材質のIOR 1.8／粗さ0.15、6バウンス、BSDFサンプリングを指定した試験も両API・両構成で成功しました。実UI編集後の`Reset all`で、これらの初期設定と4 sppのHDRが完全に戻ります。スライダーは数値入力時も有効範囲へ制限します。

Win32のWキーと右ドラッグでカメラを動かし、RのResetを同じsppで比較します。641×479、640×480へのリサイズ、最小化・1920×1080復帰、最小化直後のVulkan撮影、正常終了も対象です。`Tools/TestSamples.ps1`はプロジェクト別の期待解像度を使います。

`Tools/TestBSDFFailures.ps1`は両API・両構成で全52ケース成功しました。Shader／追加BIN／元Room設定の欠落と、不正モード・Preset・Material ID・Fixture・IOR・粗さ・距離・バウンス・サンプリング・seedでエラー終了し、解放後のValidationエラーは0件でした。

既存の`TestLoaders`、`TestWorkshopLoader`、`TestCameraNavigationCpu`、`TestPathTracingCpu`、`TestPathTracingAssets`、`TestPathTracingShaders`、`TestPathTracingFailures`は成功しました。旧RT 01〜03と発展18の専用GPU回帰はDebugで両APIを通過しました。

さらに`Tools/TestSamples.ps1`の全98サンプルをDebug／Releaseで実行し、計196ケース全てで描画・撮影・リサイズ・最小化復帰・正常終了を確認しました。各構成で既存96本は1280×720、新規2本は1920×1080へ復帰します。全Debug実行の検証レイヤーを有効にし、Validationエラーは0件です。

## 実GPU画像と再実行

主比較はRelease、1920×1080、seed 1、16バウンス、NEE＋MIS、1024 spp、露出1です。Full／Reflection／Legacy、Thickness、IOR 2.2、透明球の粗さ0.3を揃えます。1／16／256 spp、内部カメラ256 spp、操作パネル24 sppも収録します。全画像はデノイズなしです。

```powershell
.\Tools\TestBSDFCpu.ps1
.\Tools\TestBSDFAssets.ps1
.\Tools\TestBSDFShaders.ps1 -Configuration Debug
.\Tools\TestBSDFShaders.ps1 -Configuration Release
.\Tools\TestBSDF.ps1 -Configuration Debug
.\Tools\TestBSDF.ps1 -Configuration Release -CaptureDocs
.\Tools\TestBSDFFailures.ps1 -Configuration Debug
.\Tools\TestSamples.ps1 -Configuration Debug
.\Tools\TestSamples.ps1 -Configuration Release
python Tools/CollectBSDFResults.py --environment Docs/BSDFMaterialsEnvironment.json
```

別環境で再実行するときは、その実機の環境JSONを記録して集計へ渡してください。Debugの主比較は256 spp、Releaseの記事画像は1024 sppです。確率的画像のAPI間バイト一致は要求しません。JSONには各構成の数値、画像のSHA-256、実spp・seed・カメラ・物性と関連ソースのハッシュを保存します。

この記録は公開前のローカル検証結果です。サンプル・Docs・制作正本を公開リポジトリへ収録し、掲載記事は[Shader.jpのグラフィックス目次](https://www.shader.jp/blog/graphics-samples/)から案内します。
