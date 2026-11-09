# 第33回 CPUソフトウェア遮蔽判定 — Direct3D12

![実GPUの完成表示](Images/33_SoftwareOcclusion.png)

[共通解説](../TraditionalCulling.md)と[CullingLabの制作データ](../Assets/CullingLab.md)を併読してください。通常のPBR／IBLとIndexed Drawを使い、Primitive配置ごとの可視性を学びます。

## 判定の原理

CPUで不透明な遮蔽物を低解像度Depthへ描きます。対象AABBの画面矩形に重なるすべてのセルより対象の最近Depthが奥なら、CPUからDrawを発行しません。既定の解像度は画面の縦横1/4です。

## データの流れ

```text
CPU Frustum → 遮蔽物を6面でクリップ → 面の向きとセル全被覆を検査 → CPU Depth → 対象の投影矩形を検査 → 可視対象だけIndexed Draw
```

CPUの境界・判定データは`Samples/Shared/CullingData.h`にあります。ローカル境界の8隅を現在のWorld変換で動かし、描画と同じカメラ・扉・対象位置を使います。Near面交差、カメラ内包、確定できない投影は可視候補に残します。

## API実装

判定は`SoftwareDepth::Triangle`／`Hidden`で完結します。`DrawObjects(2)`がCPUの可視フラグで`DrawIndexedInstanced`を省略します。Depth診断時だけUPLOADバッファへCPU Depthを書き、fullscreen Pixel Shaderで表示します。通常の可視性判定にはGPUからのDepth読戻しを使いません。

GPUリソース、Barrier、描画、完了待ち、破棄はこのプロジェクトの`Sample.cpp`／`Culling.cpp`にあります。`Samples/Shared/CullingPanel.h`はCPUのUI、`Shaders/Culling`は両API共通HLSLです。

```powershell
msbuild Samples/Direct3D12/33_SoftwareOcclusion/Direct3D12_33_SoftwareOcclusion.vcxproj -p:Configuration=Debug -p:Platform=x64
bin/x64/Debug/Direct3D12_33_SoftwareOcclusion/Direct3D12_33_SoftwareOcclusion.exe --culling software
```

VS 2026／v145／Windows SDK 10.0.26100.0／C++20、x64 Debug・Release、HLSL 2021／SM6.6、1920×1080で動作します。

## 可視化と操作

比較モードは**OFF／Frustum／Software**です。Scene、Camera、Camera path、Door X、Object 0 X、Resetを共通UIで操作します。`--culling`と`--culling-view`で同じ設定を再現できます。

CPU resolutionで1/2・1/4・1/8を切り替えます。セルの四隅が一つの三角形の内側にある場合だけ書き、Depthにはその三角形の最遠値を使います。共有辺付近のセルを残すため除外率は下がります。GPUと同じ裏面カリング・両面材質設定を守り、Nearクリップで露出した裏面を勝手な遮蔽物にしません。

![実GPUの診断表示](Images/33_SoftwareOcclusion-depth.png)

Boundsの緑は内側、水色は視錐台との交差、赤は外側、橙は遮蔽除外、青は指定遮蔽物です。CPUの色Draw記録数とGPUの可視候補数は別に表示します。候補には画素を生成しない対象が残る場合もあります。

## 正確性と性能

カリングOFFを基準として、Object ID・Depth・完成HDRを比較します。見えている対象を誤って除外した件数はゼロを必須とし、見えない対象を残す保守的な判定は許容します。CPU版の全解像度、奇数サイズ、Near面、開口部、移動する扉・対象、急なカメラ移動、初回フレームを検査します。

`Tools/TestCulling.ps1`は画像とGPUデータの比較、`-UiOnly`は実UI入力とCLI再現、`-MotionOnly`は同一プロセス内のカメラ移動・振り返り・Resetを検査します。`Tools/TestSamples.ps1`はリサイズ・最小化復帰・終了を確認します。

追加の判定処理が描画削減分より高価なら遅くなります。[実機検証記録](../CullingVerification.md)と[5反復の計測表](../Measurements/Culling/Summary.md)で、CPU判定・記録とGPU各パスを比較してください。
