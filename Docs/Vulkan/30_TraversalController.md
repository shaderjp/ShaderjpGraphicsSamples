# 第30回：カプセルと静的な箱の連続衝突判定（Vulkan）

`ShaderjpGraphicsSamples.sln`の`Vulkan_30_TraversalController`をx64 Debug／Releaseで実行します。初期サイズは1280×720です。

![実機画像](Images/30_TraversalController-jump.png)

CPU側の入力、姿勢検索、慣性化、IK、移動判定は[共通解説](../TraversalController.md)を参照してください。各段階の役割と制作工程は[ロードマップ](../MotionMatchingRoadmap.md)にまとめています。

## 描画と同期

モデルは新しいSilverRobotMotionSet、スキニングはVertex ShaderのLBSです。描画の初期化、GPUリソース、Descriptor、Pipeline、Command、同期、解放は、このプロジェクトのSample.cppで追えます。HLSLは両APIで共有し、既存のSM6.6／HLSL 2021のオフラインコンパイルを使います。前フレームのFence完了を待ってから配置・ボーン・UIを転送し、同じ姿勢でShadow／Colorを描きます。Viewportは既存の負の高さを維持します。

地上の検索結果とジャンプ姿勢は、CPUの配置行列とローカルTRSからパレットへ反映します。第30回の練習コースにはボーン変形を適用しません。

## 検証

```powershell
./Tools/ValidateProjects.ps1
./Tools/TestMotionMatchingCpu.ps1
./Tools/TestMotionMatching.ps1 -Configuration Debug
./Tools/TestMotionMatching.ps1 -Configuration Release -CaptureDocs
./Tools/TestSamples.ps1 -Configuration Debug -Topics 30_TraversalController
```

`--test-character`は共通の自動入力ハーネスです。通常のデバイス入力を置き換え、固定刻みで同じ入力を再生します。実パッドの手動試験とは区別します。[検証記録](../MotionMatchingVerification.md)
