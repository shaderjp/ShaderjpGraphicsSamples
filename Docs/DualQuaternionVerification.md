# Dual Quaternion Skinning：検証と性能

2026-10-15。[DQS共通解説](DualQuaternionSkinning.md)、[Direct3D12](Direct3D12/24_DualQuaternionSkinning.md)、[Vulkan](Vulkan/24_DualQuaternionSkinning.md)の基本24を対象にしています。既存のBlender／glTF／GLB／UAL素材は変更していません。

## 再現するコマンド

```powershell
.\Tools\BuildSkinning.ps1 -Configuration Debug -Restore
.\Tools\BuildSkinning.ps1 -Configuration Release
.\Tools\ValidateProjects.ps1
.\Tools\TestDualQuaternionCpu.ps1
.\Tools\TestDualQuaternion.ps1 -Configuration Debug
.\Tools\TestDualQuaternion.ps1 -Configuration Release
.\Tools\TestSamples.ps1 -Configuration Debug
.\Tools\TestSamples.ps1 -Configuration Release
.\Tools\TestSamples.ps1 -Configuration Release -Topics 24_DualQuaternionSkinning -CaptureDocs
.\Tools\MeasureDualQuaternion.ps1
```

CPU／GPUの検査には既存Blender 5.2のPythonとNumPyを使います。実行サンプルの追加依存ではありません。

検査が成功したら大きな一時読戻しデータを削除し、PNG・ログ・JSONを残します。バイナリも残す場合は`TestDualQuaternion.ps1 -KeepReadbacks`を指定します。失敗時は比較途中のファイルを残し、実行ファイル／Shaderを更新していなければ`-Resume`で中断済みケースを再利用できます。

## CPUの剛体変換と変形

`Tests/DualQuaternionTests.cpp`で次を確認しています。

- 160度の回転差を混ぜる既知の例で、LBSの収縮とDQSの剛体回転を区別。
- Real／Dualの両方の符号反転に対する不変性。並進付き、4インフルエンスの既知解、UV・接線handednessの保持。
- ブレンド後のRealの単位長、RealとDualの直交、法線の単位長、接線の直交性。
- スケール、shear、反転、NaN、不正ジョイント、負ウェイト、ウェイト総和ゼロを診断。
- 既存8クリップと実行時DQS_Twistの全paletteを30 fps、計427姿勢で剛体検査。頂点は各Primitiveの31頂点ごとに抽出。

1ボーン100%の頂点を延べ418,887件比較し、LBSとの最大位置差は`2.6491e-6 m`でした。抽出した複数ボーン頂点を含むLBS／DQSの最大位置差は`0.0206097 m`です。この値は全連続時刻・全頂点の最大値を保証する値ではありません。

## 実GPUとの照合

数値結果は[Debug](DualQuaternionVerification-Debug.json)と[Release](DualQuaternionVerification-Release.json)です。既存のBlender照合済みNumPy glTF評価器でpaletteを独立再計算し、C++の座標変換・逆バインド・Mesh Global逆行列を検証します。DQSはNumPyの対称行列固有値分解からQuaternionを生成し、C++／HLSLとは別の経路で参照値を作ります。

`--capture-gpu`では共有変形関数をCSから実行して読戻します。位置・法線・接線・UV・handednessの全12成分をCPU参照と比較し、許容誤差は`3e-5`です。1体は全頂点、64体は各Actorの64頂点ごとを比較します。VS出力を直接読戻した検査ではないため、実描画もDepth／Shadow／HDRで確認します。

最大成分誤差はDebug／Releaseとも、C++参照`1.36172e-6`、GPU読戻し`1.40642e-6`でした。位置はm、法線・接線は無次元です。両APIの画像差はDepth `3e-6`、Shadow `5e-6`、HDR `0.012`の許容値を超える成分が0件でした。API間の浮動小数点・Raster差はJSONの最大差と差分成分数へ残しています。

基本24のLBSと基本22の固定時刻Walkは、両APIでDepth・Shadow・HDRが完全一致しました。ねじり確認ではLBS／DQSでDepthとShadowに差が出ます。ImGuiの実Checkbox入力でLBSへ変更し、DQSへ戻すと基準画像に完全復帰、Twistボタンで寄った確認姿勢へ移り、ResetでもWalkの基準画像へ復帰します。比較はUIを含まないGPU画像データを使います。

既存6クリップ、A／Tポーズ、実行時ねじり、1／64体、固定時刻Seek、ループと終端、Root motion ON／OFFを検査しています。Root motionの実CheckboxとStep、OFF、Resetも確認します。

## 描画・ビルドと回帰

環境は[GPUスキニングの検証環境](SkinningVerification.md#検証環境)と共通です。VS Community 2026 18.10.0、MSBuild 18.10.1、cl 19.51.36257、v145、Windows SDK 10.0.26100.0、RTX 4070／driver 610.88。D3D12 Agility 1.619.6／NuGet DXC 1.9.2609.5、Vulkan SDK 1.4.341.1／target1.3、SM6.6／HLSL 2021を維持しています。

新2プロジェクトを加えた68 APIアプリ＋2 CPUライブラリを同じソリューションでRestore・x64 Debug／Releaseビルドし、警告・エラー0件でした。Vulkan SPIR-V検証もビルドに含まれます。全vcxproj／filtersをXML照合し、項目の欠落・重複・未宣言Filter・GUID重複は0件でした。

旧ローダー、既存スキニングCPU、既存6サンプルのWalk数値比較、09 ImGuiの両構成、新2サンプルのカメラ入力・Shader欠落も通過しました。全68アプリを両構成でライフサイクル試験し、1280×720描画、640×480 resize、最小化、1280×720 restore、capture、終了を確認しました。DebugはD3D12 Debug LayerとVulkan同期Validationを有効にし、正常試験の検証エラーはすべて0件です。Shader欠落時はエラー終了と正常な解放を確認し、Shaderを復元しています。

## ReleaseのLBS／DQS比較

`MeasureDualQuaternion.ps1`は基本24内のLBS／DQSを比較します。同一のUAL_Walk、Root motion OFF、Shadow＋PBR、Studio IBL、1280×720、1／64体、Hero／StressHero固定カメラです。1/60秒刻み、60 warmup＋1200測定フレーム、VSyncなし、UIなし、GPU読戻しなしです。

CPUでは両モードとも同じ行列とDQ paletteを生成します。姿勢評価、Instance更新、DQ変換を分けて記録します。GPUはShadow、カラー、Totalを比較します。通常描画は変形頂点キャッシュを使用しません。DQ paletteは1体1,696 byte、64体108,544 byteで、比較用LBS paletteも併存します。物理メモリ使用量ではなく有効データ量です。

[全測定値・median／average／p95](DualQuaternionPerformance.json)。以下は中央値、単位msです。Instance更新（JSONの`bounds-cpu`）はMeshlet境界評価を含みません。

| API | 体数 | 方式 | CPU姿勢 | CPU DQ変換 | Shadow | Color | GPU Total |
|---|---:|---|---:|---:|---:|---:|---:|
| Direct3D12 | 1 | LBS | 0.0079 | 0.0017 | 0.2632 | 0.0799 | 0.3809 |
| Direct3D12 | 1 | DQS | 0.0082 | 0.0018 | 0.2458 | 0.0799 | 0.3625 |
| Direct3D12 | 64 | LBS | 0.2982 | 0.1018 | 0.6533 | 0.5018 | 1.1960 |
| Direct3D12 | 64 | DQS | 0.2754 | 0.0943 | 0.6298 | 0.4925 | 1.1632 |
| Vulkan | 1 | LBS | 0.0067 | 0.0016 | 0.2704 | 0.0676 | 0.3806 |
| Vulkan | 1 | DQS | 0.0069 | 0.0016 | 0.2966 | 0.0676 | 0.4063 |
| Vulkan | 64 | LBS | 0.2783 | 0.0944 | 0.6306 | 0.4639 | 1.1400 |
| Vulkan | 64 | DQS | 0.3016 | 0.1021 | 0.6177 | 0.4700 | 1.1335 |

この1回の測定では64体のGPU TotalはDirect3D12でLBS 1.1960 ms／DQS 1.1632 ms、VulkanでLBS 1.1400 ms／DQS 1.1335 msでした。1体のVulkanではDQSのTotalが増えています。差は小さく、実行順・クロック・Raster負荷の影響も含むため、DQSが常に高速という結論には使いません。

DQのジョイントデータは行列の半分ですが、Quaternionの符号合わせ・正規化・回転演算が増えます。このアセットは1ボーン頂点が多くても両方式とも4スロットの汎用処理を使います。実際の選択では変形の品質と、対象アセット・パス数・帯域・CPU palette評価を合わせて判断してください。

## Compute／Mesh版を追加した後の記録

2026-10-16に基本25と発展11を追加しました。[3方式の追加検証と32条件の再測定](DualQuaternionStagesVerification.md)を参照してください。このページの基本24単独の測定は2026-10-15の記録として保持します。
