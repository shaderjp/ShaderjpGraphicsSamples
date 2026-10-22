# 第29・30回の検証記録

検証日：2026-10-22（日本時間）。基本編30テーマと発展編17テーマ、両APIで94本の構成です。今回のGPU操作試験は第29・30回の4アプリ、ウィンドウ操作試験は第28〜30回の6アプリを対象にしました。

## 環境とビルド

Windows 11 Pro 10.0.26300、Visual Studio 2026 18.10.12201.205、v145／MSVC 14.51.36231、cl 19.51.36257.0、C++20、Windows SDK 10.0.26100.0。GPUはNVIDIA GeForce RTX 4070、ドライバ32.0.16.1088です。Blenderは5.2.1 LTS 9e2066aef7efです。

D3D12はAgility SDK 1.619.6／DXC 1.9.2609.5、VulkanはSDK 1.4.341.1と同梱DXCを使用しました。SM6.6、HLSL 2021、Vulkan 1.3です。[詳細環境](MotionMatchingEnvironment.json)

ソリューションをRestoreし、x64 Debug／Releaseをビルドしました。SPIR-V検証、全vcxproj／filtersのXML・項目対応・GUID検査、ローダー試験も成功しました。

## 素材とCPU

- 保存済みBlenderからの再出力は配布データとバイト一致し、検証による正本の書換えはありません。
- モデルglTF／GLBとコースglTFはKhronos Validatorのエラー・警告とも0件です。
- 13クリップのBlenderと独立glTFスキニング比較は最大誤差1.1146136e-06 mでした。
- 地上検索は281フレーム・30要素。全候補に対する検索、30／60／144 fps、Rootの向き、足IK、慣性化、古い検索データの拒否を確認しました。
- 足IK残差は最大4.9310518e-07 m。回転した壁のスイープ、通常ジャンプと着地、20 cm段差、壁での停止、落下復帰とResetをDebug／Releaseで確認しました。
- 段差を歩いて上った最大高さは0.200459 m、ジャンプ試験のカプセル侵入は最大0 mです。

[素材の結果](MotionAssetsVerification.json)／[CPUの結果](MotionMatchingCpu.json)

## GPUとウィンドウ

第29回を300フレーム、第30回を900フレームで両API・両構成実行し、8ケース成功しました。DebugではD3D12 Debug Layer／Vulkan Validation Layerと同期検証を有効にし、Validationエラーは0件です。両APIのCPUトレースは一致し、歩行画像の変化とReset画像のRGB一致を確認しました。

第28〜30回の両API・両構成、計12ケースで1280×720描画、640×480リサイズ、最小化・復帰、1280×720への復帰、PNG取得と終了を確認しました。

画像は更新した実行ファイルから得た1280×720の実GPUキャプチャです。自動入力による試験であり、第29・30回の物理パッドによる手動操作試験は今回の結果に含めません。

[Debugの結果](MotionMatchingGpuDebug.json)／[Releaseの結果](MotionMatchingGpuRelease.json)／[UI画像](MotionMatchingUiCaptures.json)／[ログ](Measurements/MotionMatching/ValidationLogs.zip)

## 再現コマンド

```powershell
./Tools/ValidateProjects.ps1
./Tools/BuildSkinning.ps1 -Configuration Debug -Restore
./Tools/BuildSkinning.ps1 -Configuration Release
./Tools/TestLoaders.ps1
./Tools/TestMotionAssets.ps1
./Tools/TestMotionMatchingCpu.ps1 -Configuration Debug
./Tools/TestMotionMatchingCpu.ps1 -Configuration Release
./Tools/TestMotionMatching.ps1 -Configuration Debug
./Tools/TestMotionMatching.ps1 -Configuration Release -CaptureDocs
./Tools/TestSamples.ps1 -Configuration Debug -Topics 28_CharacterController,29_MotionMatching,30_TraversalController
./Tools/TestSamples.ps1 -Configuration Release -Topics 28_CharacterController,29_MotionMatching,30_TraversalController
./Tools/CaptureMotionMatchingUi.ps1
```
