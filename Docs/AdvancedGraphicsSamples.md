# グラフィックスサンプル発展編：Direct3D 12／Vulkan

基本編で学んだリソース転送・同期・PBR・IBL・Deferred Renderingを土台に、GPUのレイトレーシングなどを実装するシリーズです。発展編は01から番号を付け、各テーマを両APIの独立した実行プロジェクトで比較します。

| 発展編 | Direct3D 12 / DXR | Vulkan |
|---|---|---|
| 01 Albedoと一次レイ | [Raytracing Basic](Direct3D12/Advanced01_RaytracingBasic.md) | [Raytracing Basic](Vulkan/Advanced01_RaytracingBasic.md) |
| 02 BRDFとレイによる影 | [Raytracing Shadows](Direct3D12/Advanced02_RaytracingShadows.md) | [Raytracing Shadows](Vulkan/Advanced02_RaytracingShadows.md) |
| 03 1バウンスの鏡面反射 | [Raytracing Reflections](Direct3D12/Advanced03_RaytracingReflections.md) | [Raytracing Reflections](Vulkan/Advanced03_RaytracingReflections.md) |

## 読み順

[基本編の目次](https://www.shader.jp/blog/graphics-samples/) → [Workshopのアセット](Assets/Workshop.md) → [Deferred Rendering](DeferredRendering.md) → [レイトレーシングの共通章](Raytracing.md) → 発展編01 → 02 → 03の順に読みます。基本編のコードと記事は[README](../README.md)から参照できます。

01では光の計算を加えず、BLAS／TLAS、Shader Table／SBT、一次レイ、重心座標とAlbedoを確認します。02で表面のBRDFとライトへの可視性を組み合わせ、03で別の表面から戻る光を1回の反射レイで調べます。3本とも一次レイから描画します。

## 環境と今回の範囲

VS 2026、v145、Windows SDK 10.0.26100.0、C++20、x64 Debug／Releaseを使用します。D3D12はAgility SDK 1.619.6、NuGet DXC 1.9.2609.5、SM6.6とDXR Tier 1.0以上が必要です。VulkanはSDK付属DXC、Vulkan 1.3、KHRのAcceleration Structure／Ray Tracing Pipeline／Deferred Host Operations、Buffer Device Addressと非一様なTexture配列参照が必要です。03の最大レイ再帰深度は3です。

初期解像度は1280×720、Workshop Standard／Hero／OpaqueMaskです。MASKはAny-hitで処理し、BLENDの透明カバー・屈折・確率的な粗さ付き反射・時間蓄積・デノイズはこの3回の対象外です。詳細な規約とGPU試験は[共通章](Raytracing.md)と[検証記録](RaytracingVerification.md)を参照してください。

Mesh Shaderはこの3回に続くテーマとして追加予定です。サンプルと記事の内容は後続の指示で決定します。
