# Workshopで学ぶDXR／Vulkanレイトレーシング

Deferredの形状・材質・カメラを引き継ぎ、画素の表面をカメラから発射するレイで決めます。APIのGPU処理は各サンプルに置き、CPUデータ・ウィジェット・HLSLだけを共有します。[発展編目次](AdvancedGraphicsSamples.md)

## BLASとTLAS

固有Primitiveごとに三角形のBLASを作り、Standard／Stressの配置をTLASのInstanceにします。同じ部品のBLASを配置間で共有するため、Stressでも形状を複製しません。頂点は位置＋U、法線＋V、接線の3つのfloat4へ詰め、48-byte strideをCPU・DXIL・SPIR-Vで揃えます。元のWorkshopデータとBlender正本を変更せず、CPU側でGPU入力へ変換します。

row-majorのWorld行列は、APIのInstance Transformが要求する3×4の並びへ転置して渡します。法線は逆転置で変換します。シーン・Opaque／OpaqueMask・遮蔽物の位置が変わった場合だけTLASを再構築し、カメラ・ライト・露出の変更ではBLAS／TLASを作り直しません。

## Shader Table／SBTと一次レイ

DXRはDXIL Library、Hit Group、Global Root Signature、Payload／Attributeサイズ、再帰深度をState Objectへ登録します。VulkanはSPIR-Vの各ステージとShader GroupをKHRのRay Tracing Pipelineへ登録します。Ray Generation、Miss、Hit GroupのShader Table／SBTには識別子だけを保存し、Local Root Signatureや材質レコードは使いません。

DXRは32-byte識別子・32-byte record alignment・64-byte table alignmentを守ります。Vulkanのhandle size、handle alignment、base alignment、Scratch alignmentはデバイスから取得し、固定値を前提にしません。

画素中心を逆ViewProjectionでNear／Farへ変換し、カメラからの方向とTMin／TMaxを求めます。左手系、深度0〜1、Y上向きの規約を保ちます。Ray GenerationにはViewportがないため、画素座標をNDCへ変換するときにYを反転します。表示とImGuiのVulkan Viewportは従来通り負の高さです。

## 重心座標・材質・MASK

`InstanceID()`で配置、`PrimitiveIndex()`で三角形を取得し、三角形の3頂点を重心座標`(1-u-v, u, v)`で補間します。AlbedoはsRGB Textureから線形値として読み、Base Color Factorを掛けます。02以降はNormal／ORM／Emissiveも同じUVで参照します。RTでは画素微分を使わず、材質TextureのLODを0と明示します。

Texture配列は64枠で、`t0`／Vulkan binding 1、Samplerは`s0`／binding 2、定数は`b0`／binding 0、すべてset 0です。残りのSRVは`t64`以降に置き、配列とregisterを重ねません。未使用枠も有効なTextureを参照します。異なるレイが異なる材質を読むため、Textureの添字に`NonUniformResourceIndex`を付けます。

OPAQUEのBLAS GeometryにはOpaqueフラグを付け、MASKには付けません。MASKのAny-hitはAlbedo Alpha×Factor Alphaをcutoffと比較し、穴なら`IgnoreHit()`します。一次・影・反射のHit Groupに同じAny-hitを接続します。両面材質はInstanceのTriangle Cull Disableを使い、裏面のシェーディング法線も反転します。

## BRDFとシャドウレイ

02はDeferredと同じGGX、Schlick Fresnel、height-correlated Smith、Lambertの1／πを使います。点光源は逆二乗減衰と既存のrangeを維持します。Vは各ヒットへ入射したレイ方向の逆向きです。03の反射先で元のカメラ位置をVに使うと、反射先のBRDFが誤って評価されます。

ディレクショナルライトと点光源について、表面からライトへのレイを発射します。最初の有効ヒットで検索を終了し、Closest-hitは省略します。Payloadを遮蔽状態で初期化し、Shadow Missだけが可視状態へ変更します。点光源のTMaxはライトまでの距離から原点オフセットを引き、ライトの先にある物体で影を作りません。

影と反射の原点は幾何法線を使って外向きへずらします。Normal Mapの法線はBRDFに使用します。初期オフセットは0.001 mで、UIから調整できます。01は一次ヒットだけで深度1、02は一次→影で深度2です。

## 1バウンスの鏡面反射

03は一次ヒットで反射方向を求め、RadianceのHit Groupへ1回だけTraceRayします。反射先は02のBRDF・影・IBL・Emissionを評価し、追加の反射は発射しません。MissはWorkshopの環境Cubemapを返します。一次→反射→影が最深経路なので、再帰深度は3です。

一次ヒットの鏡面IBLを`Fresnel(N・V, F0) × 反射先の放射輝度`へ置き換え、直接光・拡散IBL・Emissionへ加えます。鏡面IBLとの二重加算を避け、反射OFFでは02へ戻ります。反射先の鏡面IBLは維持します。

これは理想鏡面の教材用近似です。Roughnessは直接光のGGXと02のIBLに適用しますが、追加した反射レイの広がりには適用しません。確率的なGGX反射、蓄積、デノイズ、多重反射は後続テーマです。IBLは事前計算環境の近似で、局所的な拡散間接光やGIを計算するものではありません。

## HDR・表示・同期

01はAlbedoをそのままsRGB出力へ渡します。02・03はRGBA16Fへ線形HDRを書き、既存の半解像度Bloomを実行し、最後に露出・Reinhard・sRGB変換を一度だけ適用します。UIはその後です。診断表示で色や法線・深度を確認するときは必要に応じてTone Mapを省略します。

BLAS→TLAS→TraceRays、RTのStorage／UAV書込み→Bloomの読込み→表示、GPU→CPU読戻しをそれぞれ同期します。D3D12はASのUAV Barrierと画像のResource Barrier、VulkanはSynchronization2のAS Build／Ray Tracing Shader／Compute／Fragment／Hostを接続します。VulkanのPNG取得はAcquire済み画像をPresent前に読みます。

毎フレームGPU完了を待ってから定数・ライト・UI Buffer・Descriptorを更新します。この初期実装は1フレームずつ待機する教材用構成です。

## 検証

`Tools/TestRaytracing.ps1`は実GPUからHDR・UV／ヒット距離／材質ID・反射先の光を読み戻し、既知の平面・UV画像・MASK・遮蔽物・鏡面の試験シーンと照合します。`--rt-test-scene`はこの診断専用で、通常はWorkshopを描画します。`--capture-gpu`を付けた撮影時だけ追加のGPU読戻しを行います。Stage別SPIR-Vのoffset／strideとKHR機能宣言も確認します。

参考：[DXR Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)、[Vulkan Ray Tracing](https://docs.vulkan.org/spec/latest/chapters/raytracing.html)、[DXCのHLSL／SPIR-V対応](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)
