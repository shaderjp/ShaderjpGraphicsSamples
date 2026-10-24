# 発展18 パストレーシングの検証記録

検証日：2026-10-24（日本時間）。Direct3D12／Vulkanの`Advanced18_PathTracingBasic`を対象とします。[共通解説](PathTracing.md)／[アセット](Assets/PathTracing.md)／[数値結果JSON](PathTracingResults.json)

## 実測した環境

| 項目 | 使用・取得したバージョン |
|---|---|
| OS | Windows、10.0.26300.0 |
| Visual Studio | 2026 Community、18.10.12201.205 |
| C++ | v145、C++20、cl.exe 19.51.36257.0、x64 Debug／Release |
| Windows SDK | 10.0.26100.0 |
| GPU／ドライバ | NVIDIA GeForce RTX 4070／NVIDIA 621.01 |
| Agility SDK | Microsoft.Direct3D.D3D12 1.619.6、D3D12SDKVersion=619 |
| D3D12 DXC | 1.9.2609.5、01b62ad47 |
| Vulkan SDK／DXC | 1.4.341.1／1.9.0.5180、e35541826（DLL表示1.10） |
| シェーダ | SM6.6、HLSL 2021、row-major、Vulkanターゲット1.3 |
| 制作・アセット検証 | Blender 5.2.1 LTS、glTF-Validator 2.0.0-dev.3.10 |

## ビルド・構造体・アセット

- NuGet Restoreとソリューション全99プロジェクト（96 APIサンプル＋3 Support）のx64 Debug／Releaseビルドを実行しました。最終のビルドにエラー・警告はありません。
- `Tools/ValidateProjects.ps1`で全project／filtersのXML、全項目の一対一対応、GUID、親フィルタ、ファイル存在、ソリューションのAPIフォルダ配置を検証しました。新規プロジェクトはD3D12が120項目、Vulkanが119項目です。
- `Tools/TestPathTracingShaders.ps1`で両構成のDXIL／SPIR-V、KHR機能宣言、RGBA32F蓄積、CPU／GPU配置を検証しました。Vertex 48、Material 80、Instance 144、PathLight 64 bytes、Scene定数240 bytesです。
- Windowsの`core.autocrlf=true`による隔離チェックアウトでも、Manifestの全ファイルハッシュが一致することを確認しました。ハッシュ対象のテキストはLFで固定します。
- `Tools/TestPathTracingAssets.ps1`はglTFのエラー・警告とも0件、安定ID、法線・接線、Index、参照、Manifest、保存済みblendだけを使った隔離再書出しを通過しました。正本は書出し前後で同一ハッシュです。
- `Tools/TestPathTracingCpu.ps1`で太陽・環境・経路なしの新プロファイル、旧プロファイルの拒否規則、データ結合、面光源の面積・CDF・検索先、空／退化光源を確認しました。既存の`TestLoaders`と`TestWorkshopLoader`も通過しました。

## GPUからの独立した数値照合

両API・両構成で`Tools/TestPathTracing.ps1`相当の全条件を実行し、実GPUのfloat32 HDRを検査しました。DebugではD3D12 Debug Layer、Vulkan Validation Layerと同期検証を有効にし、検証エラー0件です。

- 既知の発光面は中央のRGBが`0.25 / 0.5 / 1`と絶対差`2e-5`以内で一致しました。発光・環境・デルタ光源のない場面は0でした。
- 1.5×1.5の矩形光源、放射輝度3、Base Color `0.8 / 0.4 / 0.2`、ORM粗さ`128/255`の試験シーンを、CPUの独立した面積積分と照合しました。各表面点で256×256点を積分し、中央64×64画素の平均と比較しています。
- CPUのRGB参照値は`0.111198 / 0.057466 / 0.030600`です。1024 sppのGPU参照との許容差は相対`0.025`＋絶対`0.001`、128 sppの3 seed平均はMISが相対`0.035`＋絶対`0.001`、BSDFが相対`0.05`＋絶対`0.002`です。
- 1.4倍スケール、回転、移動を加えた光源も数値積分と照合しました。CPU参照は`0.171776 / 0.089420 / 0.048242`、許容差は相対`0.04`＋絶対`0.001`です。
- 遮蔽物、MASKの透過部分、黒い環境を確認しました。NEEを無効にした場合はMISの発光割引も外れ、BSDFモードと同じ画像になります。

次はseed 1／7／19、各128 sppの中央RGB平均です。MSEは中央120×200画素・RGBについて、MIS・1024 spp・seed 1の参照との差を求め、3 seedで平均した値です。

| 構成／API | MIS 平均RGB | BSDF 平均RGB | MIS MSE | BSDF MSE |
|---|---|---|---:|---:|
| Debug / Direct3D12 | 0.111097 / 0.057406 / 0.030560 | 0.110213 / 0.056951 / 0.030320 | 1.0020647e-05 | 0.00094490617 |
| Debug / Vulkan | 0.111097 / 0.057406 / 0.030560 | 0.110213 / 0.056951 / 0.030320 | 1.002065e-05 | 0.00094490613 |
| Release / Direct3D12 | 0.111097 / 0.057406 / 0.030560 | 0.110213 / 0.056951 / 0.030320 | 1.0020646e-05 | 0.00094490617 |
| Release / Vulkan | 0.111097 / 0.057406 / 0.030560 | 0.110213 / 0.056951 / 0.030320 | 1.002065e-05 | 0.00094490613 |

この面光源シーンではMISの誤差が小さくなりました。シーン限定の同じsppでの比較であり、一般的なFPS向上率を示す値ではありません。Roomの拡散壁でも、1→16 sppで256 spp参照との差が小さくなることを確認しました。

同じRelease条件のAPI間の線形HDR差です。交差位置や浮動小数点演算の差が鏡面反射で拡大されるため、全画素のバイト一致は合否条件にしていません。正当性は上の独立した参照と各APIを照合しています。

| 条件 | RGB絶対差平均 | 99パーセンタイル | 最大差 |
|---|---:|---:|---:|
| room-1 | 6.1858591e-05 | 5.364418e-05 | 12.139639 |
| room-256 | 2.8576122e-05 | 0.00030660629 | 0.094804764 |
| workshop | 0.0019759424 | 0.01841262 | 315.35312 |

## UI・ウィンドウ・終了

- 実際のImGuiイベントでOccluderを編集し、7 sppへ到達した画像を比較しました。GPUシーンが変化し、Reset後は同じ7 sppのHDRへ完全に戻りました。入力イベントの遅延が比較sppへ混ざらないよう、撮影は目標sppへの到達を待ちます。
- 露出変更ではHDR平均とサンプル数が不変で、表示PNGだけが変化しました。Pause中のHDR・sppは不変、Resumeでsppが増加しました。初期Pauseは最初の1サンプルだけを作ります。
- WASDと右ドラッグで移動・回転し、RによるカメラReset後は同じ4 sppの基準HDRへ戻りました。
- Room／Workshop Standard／Stress、OpaqueMask、641×479を検証し、全HDRのNaN／Inf不在を確認しました。
- `Tools/TestSamples.ps1`で640×480、最小化・1280×720復帰、撮影、正常終了を両構成で確認しました。
- Vulkanは最小化直後の最終フレーム撮影も試験しました。Win32の復帰メッセージを処理してから画像を再作成し、復帰後の1280×720・1 sppが通常の1 spp画像と完全一致しました。通常の撮影では蓄積を進めません。
- 欠落シェーダ／BIN、Full、未知の試験シーン、不正なバウンス数／Sampling／負のseedを両API・両構成で検証しました。終了コード1、対象を示す診断、部分初期化の解放、検証エラー0件です。
- 既存発展01〜03の`Tools/TestRaytracing.ps1`を両構成で再実行し、UV、材質、MASK、変換、影、反射、環境、Stress、UI／Resetの回帰試験を通過しました。

## 収録画像と今回の範囲

実GPUのPNGは各API7点、すべて1280×720です。Room 1／16／256 spp、BSDF 256 spp、直接光のみ256 spp、Workshop 128 spp、操作パネルを収録し、目視しました。理論図4点は編集可能なSVGとPNGを同梱しています。既存の画像、WorkshopとShaderBallの正本・配布データ、ThirdPartyのソースは変更していません。

ReSTIR、デノイズ、再投影、適応サンプリング、屈折・透過、参加媒質、Wavefront化は対象外です。有限バウンス上限、レイ原点のオフセット、材質LOD 0はこの初回実装の条件です。サイト反映と公開は別工程です。
