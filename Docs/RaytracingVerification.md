# 発展編01〜03 レイトレーシングの検証記録

検証日：2026-10-13（日本時間）。DXR／VulkanのAlbedo一次レイ、BRDFと影、1バウンス反射の6実行プロジェクトを対象とします。[発展編目次](AdvancedGraphicsSamples.md)・[共通章](Raytracing.md)

## 使用環境

| 項目 | 実測・使用したバージョン |
|---|---|
| Visual Studio | 2026 Community 18.10.12201.205 |
| Toolset・C++ | v145・C++20、cl.exe 19.51.36257.0 |
| Windows SDK | 10.0.26100.0 |
| GPU・ドライバ | NVIDIA GeForce RTX 4070・NVIDIA 610.88 |
| Agility SDK | Microsoft.Direct3D.D3D12 1.619.6 |
| D3D12 DXC | 1.9.2609.5・01b62ad47 |
| Vulkan SDK・DXC | 1.4.341.1・1.9.0.5180・e35541826 |
| シェーダ | SM6.6／lib_6_6、HLSL 2021、row-major |
| Vulkanの実行ターゲット | Vulkan 1.3・KHR Ray Tracing Pipeline |

Vulkanの最大再帰深度31、Shader Group Handle Size 32、Handle Alignment 32、Base Alignment 64、AS Scratch Alignment 128を取得しました。サンプルはこの問い合わせ結果を使ってアラインメントを計算します。

## ビルドとShader／CPU配置

- NuGet Restoreとソリューションのx64 Debug／Releaseビルドを実行。VS／SDK／Toolsetの変更はありません。
- 6組の新規vcxproj／filtersを含め、`Tools/ValidateProjects.ps1`でXML、全項目の一対一対応、親フィルタ、GUIDと物理ファイルを検証しました。
- 両構成でDXILとSPIR-Vを検証しました。RTのSPIR-VはKHR Ray Tracingを使用し、RayQueryKHR／NV Ray Tracingの機能宣言を含みません。
- Vertex stride 48、Primitive stride 16、Instance stride 144、Material stride 80、Point Light stride 32を確認。Sceneのoffsetは0／64／128／144／160／176／192／208／224で、CPUの240-byte配置と一致します。

## 専用GPU試験

`Tools/TestRaytracing.ps1`を両API・両構成で実行しました。DebugはD3D12 Debug Layer／Vulkan Validation LayerとVulkan同期検証を有効にし、検証エラー0件でした。

- 既知の平面と2×2の色画像で、UV、ヒット距離、線形Albedoを独立したCPU計算と照合しました。UV／距離の許容絶対差は`2e-5`、AlbedoはGPUフィルタとFP16の精度を含め`0.01`です。
- InstanceのScale／Rotation／Translation、MASKの穴と裏面、影と反射のAny-hitによるMASK判定を確認しました。
- ディレクショナルライト・点光源の遮蔽、自己交差の回避、点光源より先の遮蔽物が影を作らないことを確認しました。
- カメラの画面外にある発光面の反射先の光が`[0.25, 0.5, 1]`となり、移動後には中央から消えることを確認しました。反射先の影と環境Missも確認しました。
- 反射OFFのHDRは02と絶対差`0.001`＋相対差`0.002`の範囲で一致しました。
- Standard／Stress、641×479、全HDRのNaN／Inf不在を確認。実ウィジェットで遮蔽物を動かし、UI外のシーン画素の変化とReset後の完全一致を確認しました。
- 最終ビルド後、点光源0個のAlbedo・ディレクショナルライトShadow・環境反射を全6本・両構成で再確認し、描画・GPU読戻し・解放と検証エラー0件を確認しました。

通常のWorkshop Heroを両APIで撮影したHDRの比較値です。理想鏡面の反射は細かな法線と交差位置の変化を拡大するため、画像全体のバイト一致は試験条件にしていません。

| テーマ | RGB絶対差の平均 | 99パーセンタイル | 最大差 |
|---|---:|---:|---:|
| Albedo | 0.00013156 | 0.0024414 | 0.62817 |
| BRDF・影 | 0.00039299 | 0.0039063 | 54.25 |
| 反射 | 0.0014102 | 0.011597 | 206.58 |

一次ヒットの材質IDが異なる画素は、各テーマとも921,600画素中53画素でした。HDRの大きな最大差を表示変換後の8bit差と混同せず、同じ既知の試験シーンを両APIで別々に照合しています。

## 回帰・失敗時の解放・画像

- OBJ／glTF／WIC／ファイルとWorkshopのCPUローダー試験を通過しました。
- `Tools/TestSamples.ps1`を両構成で実行し、描画、640×480への変更、最小化・1280×720復帰、PNG取得、終了を確認しました。
- 既存09のImGui編集／Resetと、17・18のDeferred数値・表示・UI試験を両構成で通過しました。
- 隔離した配布フォルダでRayGenerationの欠落、Fullの拒否、不明なRT試験シーンの拒否を検証。全6本・両構成で終了コード1、対象を示すエラー、部分初期化の解放、検証エラー0件を確認しました。
- 新規PNGは20点、すべて1280×720。各APIの完成表示、MASK、影OFF、反射OFF、反射先の光、通常のUI付き表示を実GPUから撮影しました。既存の画像、Workshopの配布アセット、Blender正本、ThirdPartyのソースは変更していません。

BLEND／屈折、粗さ付きの確率的な反射、時間蓄積、デノイズ、多重反射、局所的な拡散GIは今回の範囲に含めません。Shader.jpのサイト反映と公開は別工程です。
