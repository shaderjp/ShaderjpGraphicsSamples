# 伝統的カリング：実機検証記録

2026-11-09〜10（日本時間）。基本31〜34のDirect3D12／Vulkan、計8アプリを実装し、以下を実機で確認しました。

## 環境とビルド

Windows 10.0.26300.0、Core i9-13900F、GeForce RTX 4070／NVIDIA 621.01（32.0.16.2101）。VS 2026 Community 18.10.12201.205、v145／MSVC 19.51.36257.0、Windows SDK 10.0.26100.0、C++20。

Agility SDK 1.620.1、D3D12 DXC 1.9.2609.5（01b62ad47）、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180（e35541826）、SPIRV-Tools v2026.1（fbe4f3ad）、Vulkanターゲット1.3、SM6.6／HLSL 2021。[環境JSON](CullingEnvironment.json)

NuGet Restore、全113プロジェクト（110 APIアプリ＋3 Support）のx64 Debug／Releaseビルド、DXIL／SPIR-Vコンパイル・spirv-val、project／filtersのXML検証を通過しました。Preview SDKを使う既存の発展20〜23の設定は維持し、新4方式は通常版SDKでビルドしています。

## 正確性

| 検証 | 結果 |
|---|---|
| GPU画像比較 | 78条件 × 2 API × 2構成＝312件。可視対象の誤除外0、ID不一致0 |
| Depth | 全GPU比較でOFFとの差が1e-6以下 |
| 完成HDR | Hero視点で4方式とOFFのFP16出力が完全一致 |
| Hi-Z | 全段の最大値縮約とFarの余白をCPU参照と完全一致で確認 |
| UI | 両構成の8アプリで扉・OFF・Reset、編集状態のCLI再現、HDRの復帰を確認 |
| カメラ | 両構成で移動・急な振り返り・Resetの32ステージをOFFと比較 |
| 奇数リサイズ | 両構成の8アプリで641×479→最小化→1920×1080復帰のID／Depthが一致 |
| ライフサイクル | 既存102＋新8アプリの全110本をDebug／Releaseで通過（計220件） |
| CPU／Loader | 6平面、投影、Nearクリップ、裏面、セル被覆、OBJ／glTF／WIC／ファイル、配置数を通過 |
| 既存ImGui | 09の両API・両構成でSlider／Resetと画像の往復を通過 |
| 検証レイヤー | 正常Debug実行のValidationエラー0。Vulkanは同期検証も有効 |

[数値結果](CullingResults.json)。18の場面はHero、開いた扉のOpening、Near、Occlusion、Plaza、1279×719、Mini、Stress、経路600／1200、初回フレーム、対象移動、および完全遮蔽・部分可視・細い隙間・全可視・視錐台外・カメラ内包の独立fixtureです。Hero・奇数解像度・隙間ではCPU Depthの1/2・1/4・1/8を比較しました。

Near面の試験で、CPU Depthが片面材質の裏面まで描いていた問題を検出しました。GPUと同じ面の向き／両面設定を適用して修正し、全比較を再実行しました。完全遮蔽fixtureは三角形の共有辺から離して配置し、CPU参照実装が意図的に残す未被覆セルと誤除外を区別しています。

Vulkan 34の大きなピラミッドはDEVICE_LOCALに配置し、撮影時だけ読戻します。この変更後にReleaseの18条件とDebugの4条件を追加比較しました。後続のDebug本試験・移動・奇数リサイズ・ライフサイクルも変更後の実装で確認しています。

## 制作データと画像

Blender 5.2.1 LTS（9e2066aef7ef）の保存済み正本から隔離して再書出しし、glTF／BIN／PNG・固定カメラ・経路をバイト比較で一致確認しました。Mini／Standard／Stressの対象数は64／512／4,096、建築物込みで86／594／4,418です。Workshopの正本は変更していません。

実GPUの1920×1080画像20枚を保存・目視確認しました。完成表示8枚と、両APIのBounds／CPU Depth／Hi-Z／俯瞰です。[画像ハッシュと出典](CullingCaptures.json)。画像内のUI時間は撮影時の瞬間値であり、以下の単独計測の代用にはしません。

## 性能

他の検証プロセスを終了した後、Release／1920×1080／Standardで60フレームのウォームアップ＋1,200フレームを5反復しました。2 API × 2経路 × 7モード × 5反復＝140ラン、168,000計測フレームです。

[CPU／GPU各区間の計測表](Measurements/Culling/Summary.md)、[反復ごとの中央値・p95と範囲](Measurements/Culling/Summary.json)、[全フレームの圧縮JSON](Measurements/Culling/Frames.json.gz)。

- Direct3D12／Hero: GPU全体の最小中央値は32 queryの0.318 ms。OnRender全体では32 queryの0.648 msでした。
- Direct3D12／Plaza: GPU全体の最小中央値は32 queryの0.928 ms。OnRender全体では34 depth-onlyの1.328 msでした。
- Vulkan／Hero: GPU全体の最小中央値は32 queryの0.310 ms。OnRender全体では32 queryの1.242 msでした。
- Vulkan／Plaza: GPU全体の最小中央値は32 queryの0.693 ms。OnRender全体では31 offの1.780 msでした。

遮蔽が少ない場合とCPU費用の例：

- Direct3D12: Plazaの34はDepthのみが0.967 ms、Hi-Zが1.503 ms（GPU全体）。HeroのSoftwareはCPU判定だけで3.073 ms、OnRender全体は4.394 msでした。同じHeroのFrustumはOnRender 1.017 msです。
- Vulkan: Plazaの34はDepthのみが0.785 ms、Hi-Zが0.854 ms（GPU全体）。HeroのSoftwareはCPU判定だけで3.127 ms、OnRender全体は4.751 msでした。同じHeroのFrustumはOnRender 1.293 msです。

GPU時間とCPUを含むOnRender時間は分けて読みます。SoftwareはDrawを減らせても単一スレッドのラスタライズ費用を払います。Query／Hi-ZはCPUの色Draw記録数を削減せず、追加パスも必要です。除外数だけを性能向上の根拠にしないため、DepthのみとOFFを併記しています。

電源プランはBalanced、CPU／GPUクロックは既定の動的制御です。上記の最小中央値は今回の測定内での値です。反復範囲が重なる場合は速度の優劣を確定せず、計測表の最小〜最大とJSONのp95も確認してください。

## 再実行と範囲

```powershell
Tools/TestCullingCpu.ps1
Tools/TestCulling.ps1 -Configuration Debug
Tools/TestCulling.ps1 -Configuration Debug -UiOnly
Tools/TestCulling.ps1 -Configuration Debug -MotionOnly
Tools/TestCulling.ps1 -Configuration Debug -ResizeOnly
Tools/MeasureCulling.ps1
& 'C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe' Tools/CollectCullingResults.py
```

Releaseでも同じ専用試験を実行します。撮影は`Tools/CaptureCulling.ps1`、正本の隔離再書出しは`SourceArt/Blender/CullingLab/validate.py --reexport`です。

実機確認は上記RTX 4070環境です。他GPUやConditional Rendering非対応機での起動失敗は未実機検証です。静的形状と剛体移動を対象とし、スキニング・透明合成・Shadowの可視性判定、前フレームDepth再利用、SIMD／並列CPU最適化はこの初版に含めません。
