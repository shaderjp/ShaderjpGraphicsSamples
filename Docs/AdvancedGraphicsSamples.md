# グラフィックスサンプル発展編：Direct3D 12／Vulkan

基本編で学んだリソース転送・同期・PBR・IBL・Deferred Renderingを土台に、GPUのレイトレーシング、Mesh Shader、GPU Driven Rendering、パストレーシングを実装するシリーズです。発展編は01から番号を付け、各テーマを両APIの独立した実行プロジェクトで比較します。現在は基本30テーマ＋発展18テーマ、両API96本です。

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

## Mesh ShaderとGPU Driven Rendering

04で単一のShaderBallを使ったMesh Shaderの基礎を学び、[Meshletとmeshoptimizerの共通記事](Meshlets.md)へ進みます。共通記事にはDXR 2のClustered Geometryとのつながりも掲載します。その後は05のCompute＋Indirect、06のAS／Task、07のShadow、08のDeferred、09のTiled Deferredの順です。

| 発展編 | Direct3D 12 | Vulkan |
|---|---|---|
| 04 Mesh Shader基礎 | [記事](Direct3D12/Advanced04_MeshShaderBasic.md) | [記事](Vulkan/Advanced04_MeshShaderBasic.md) |
| 05 GPU Driven Rendering | [記事](Direct3D12/Advanced05_GpuDrivenRendering.md) | [記事](Vulkan/Advanced05_GpuDrivenRendering.md) |
| 06 AS／TaskによるMeshlet Culling | [記事](Direct3D12/Advanced06_MeshletCulling.md) | [記事](Vulkan/Advanced06_MeshletCulling.md) |
| 07 Mesh ShaderによるShadow Map | [記事](Direct3D12/Advanced07_MeshShaderShadows.md) | [記事](Vulkan/Advanced07_MeshShaderShadows.md) |
| 08 Mesh ShaderによるDeferred Rendering | [記事](Direct3D12/Advanced08_MeshShaderDeferred.md) | [記事](Vulkan/Advanced08_MeshShaderDeferred.md) |
| 09 Mesh ShaderによるTiled Deferred | [記事](Direct3D12/Advanced09_MeshShaderTiledDeferred.md) | [記事](Vulkan/Advanced09_MeshShaderTiledDeferred.md) |
| 10 Mesh ShaderスキニングとShadow | [記事](Direct3D12/Advanced10_MeshShaderSkinning.md) | [記事](Vulkan/Advanced10_MeshShaderSkinning.md) |
| 11 Dual Quaternion Mesh SkinningとDQS境界 | [記事](Direct3D12/Advanced11_DualQuaternionMeshSkinning.md) | [記事](Vulkan/Advanced11_DualQuaternionMeshSkinning.md) |

全16本に移動・回転・カメラResetを備えます。D3D12はMesh Shader Tier、Vulkanは`VK_EXT_mesh_shader`を要求し、06以降はTask Shaderも必要です。Workshop使用回はStandard／StressとOPAQUE／MASK、04は単一のAlbedo用ShaderBall、10／11はSilver Robotです。DXR 2は概念解説で、今回の実行サンプルの必須機能には追加しません。[検証記録](MeshShaderVerification.md)

[発展10の実行動画](Assets/Videos/README.md)では、両APIの1体のWalkと64体のRunを確認できます。4本ともAS／TaskとAnimated frustumを有効にし、Root motionはOFFです。64体では位相をずらした独立姿勢とShadowを比較できます。

10は[glTFスキニング対応ローダー](GltfSkinningLoading.md) → [基本22 VS](Direct3D12/22_VertexSkinning.md) → [基本23 Compute Cache](Direct3D12/23_ComputeSkinningCache.md)を前提に、動くSilver RobotをMS内で変形します。カメラ／ライト別のカリング、変形後の保守境界、1体／64体を比較します。[3方式の共通解説](GpuSkinning.md)と[検証・性能](SkinningVerification.md)を参照してください。Mesh Shader編は04〜11の16本になりました。

11は[基本24 DQS](DualQuaternionSkinning.md)と[基本25 Compute Cache](DualQuaternionStages.md)に続き、DQSをMS内で実行します。LBSのAABBを流用せず、DQ正規化を考慮した球でカメラ／ライトを独立に可視判定します。[検証・実測](DualQuaternionStagesVerification.md)も参照してください。

## バインドレス発展編（12〜15）

[共通章](BindlessGraphics.md) → [Blenderアセット](Assets/BindlessGallery.md)から、材質、Mesh Shader、レイトレーシング、Hybridへ進みます。12〜15の8本に続き、16では同じHybridをRayQueryで実装します。

| テーマ | Direct3D12 | Vulkan |
|---|---|---|
| 12 BindlessMaterials | [記事](Direct3D12/Advanced12_BindlessMaterials.md) | [記事](Vulkan/Advanced12_BindlessMaterials.md) |
| 13 BindlessMeshShader | [記事](Direct3D12/Advanced13_BindlessMeshShader.md) | [記事](Vulkan/Advanced13_BindlessMeshShader.md) |
| 14 BindlessRaytracing | [記事](Direct3D12/Advanced14_BindlessRaytracing.md) | [記事](Vulkan/Advanced14_BindlessRaytracing.md) |
| 15 BindlessHybrid | [記事](Direct3D12/Advanced15_BindlessHybrid.md) | [記事](Vulkan/Advanced15_BindlessHybrid.md) |

## インライン・レイトレーシング（発展16〜17）

[共通解説](InlineRaytracing.md)と基本26・27から進みます。発展16は発展15のRayQuery版、発展17は発展09のTiled Deferredに選択した点光源の影を加えます。

- Bindless・Mesh Shader・RayQueryを接続する：[Direct3D12](Direct3D12/Advanced16_BindlessInlineHybrid.md)／[Vulkan](Vulkan/Advanced16_BindlessInlineHybrid.md)
- Tiled Deferredの多数光源へレイによる影を加える：[Direct3D12](Direct3D12/Advanced17_MeshShaderTiledInlineShadows.md)／[Vulkan](Vulkan/Advanced17_MeshShaderTiledInlineShadows.md)

## パストレーシング（発展18）

レイトレーシング01〜03、Inline Raytracing編に続き、[共通解説](PathTracing.md)で光の経路、Monte Carlo、NEE、MIS、蓄積を学びます。[専用室内とWorkshop](Assets/PathTracing.md)を両APIと後続記事で共有します。

| 発展編 | Direct3D12 | Vulkan |
|---|---|---|
| 18 PathTracing Basic | [記事](Direct3D12/Advanced18_PathTracingBasic.md) | [記事](Vulkan/Advanced18_PathTracingBasic.md) |

初回は拡散・粗い鏡面反射、面光源、多重反射、全解像度の蓄積です。ReSTIR・デノイズ・再投影は含めません。[実機検証](PathTracingVerification.md)
