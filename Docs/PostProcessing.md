# HDRのRender-to-TextureからComputeの画像とBufferへ

第13回までで、材質、IBL、方向光のShadowがつながりました。第14〜16回では、その描画結果とGPU上のデータを、次の処理の入力として使います。形状を直接Swapchainへ描く構成から、途中の画像を保持して処理を重ねる構成へ進みます。

## 第14回で表示変換の場所を移す

HDRは単に白が明るく見える画像ではありません。照明計算で1を超える値を保持し、最後に表示範囲へ収めるためのデータです。RGBA16Fへ線形色を保存し、最後の全画面パスでExposure、Reinhard、sRGB出力を行います。UIは表示変換の後へ描きます。

Render Target ViewそのものをTextureとして読むわけではありません。同じTextureへ、書くためのRTVと読むためのSRVを作ります。Vulkanでは、Imageの用途、Image View、描画AttachmentとDescriptorを対応させます。どちらのAPIでも、ViewやDescriptorの接続とは別に、書込みと読込みの同期が必要です。

別カメラの映像もHDRで保存します。モニターのUnlit描画で読み、メイン画像へ置いてから、一度だけ表示変換します。映像取得パスからモニターを除外し、循環する読書きを作りません。

## 第15回で画像へComputeから書く

HDRの明るい領域を取り出し、別の画像へ縮小して書きます。横・縦の9 tap Gaussianを別パスへ分け、出力を次の入力へ渡します。黒は黒のまま、一定色はFP16の精度内で維持され、端と奇数サイズでも範囲外を読まないことを確認します。

Compute ShaderはPixel ShaderのようにRasterizerから画素を受け取るのではなく、DispatchとThread Groupで仕事の範囲を決めます。画像の大きさとスレッド数が一致しない場合は、Shader側の境界判定も必要です。今回の画像処理は8×8 threads／groupです。

## 第16回でBufferを頂点入力へ渡す

GPUで粒子の位置・速度・寿命を更新し、同じBufferをインスタンス用頂点入力へ渡します。更新の書込みと頂点入力の読込みをバリアで接続します。通常フレームでCPUへの読戻しや再転送を行わず、データをGPU上に置いたまま使います。

各粒子は独立しているため、一つのスレッドがその粒子だけを更新します。近隣粒子を参照する流体や衝突などへ拡張するときは、入力・出力の分離や、追加の同期を考えます。初回は256 threads／group、16,384粒子とし、257粒子の境界条件も試験します。

## 実装を比較する

| 回 | Direct3D 12 | Vulkan |
|---|---|---|
| 14 Render-to-Texture | [記事](Direct3D12/14_RenderTexture.md) | [記事](Vulkan/14_RenderTexture.md) |
| 15 Compute Bloom | [記事](Direct3D12/15_ComputeBloom.md) | [記事](Vulkan/15_ComputeBloom.md) |
| 16 GPU Particles | [記事](Direct3D12/16_GpuParticles.md) | [記事](Vulkan/16_GpuParticles.md) |

共有するのはHLSL、UI、CPUの配置とデータです。GPUのImage／Buffer、View／Descriptor、Pipeline、描画、Dispatch、同期は各サンプルに残しています。[モニター用アセット](Assets/Monitor.md)のBlenderデータも編集できます。

[D3D12のResource Barrier](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)、[Vulkanの同期例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)、[Computeによる粒子更新](https://docs.vulkan.org/tutorial/latest/11_Compute_Shader.html)も参照してください。

次の候補はSSAOです。カメラ深度と法線、位置復元を使い、接地や周辺の遮蔽を画面から近似します。静的AOやShadowとの違いを、同じ展示シーンで観察できます。
