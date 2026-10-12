# Workshopで学ぶDeferred Rendering

第17回はPixel Shader、第18回はCompute ShaderでG-bufferをシェーディングします。各回をDirect3D 12／Vulkanで実装し、材質・照明・Shadow・HDR・Bloomを同じWorkshopへ集約します。

| サンプル | Direct3D 12 | Vulkan |
|---|---|---|
| 第17回 Pixel Shader | [記事](Direct3D12/17_DeferredPixel.md) | [記事](Vulkan/17_DeferredPixel.md) |
| 第18回 Compute Shader | [記事](Direct3D12/18_DeferredCompute.md) | [記事](Vulkan/18_DeferredCompute.md) |

## 描画する形状と、光を計算する画素を分ける

これまでのForwardでは、モデルのPixel Shaderがテクスチャを読み、法線を作り、ライトを評価していました。Deferredでは、まず画面で見える表面の材質と法線をG-bufferへ保存し、その後で画素ごとに光を計算します。奥の形状の材質評価とG-buffer書込みは深度テストの影響を受けますが、最終ライティングは最前面の表面に対して一度だけ行えます。

今回の点光源はStandardで8個、Stressで72個です。両版とも同じ全ライトのループを使います。ライト数を増やしたときの画素単位の計算量を残すことで、後続のTiled／Clustered Renderingによるライト絞り込みと比較できます。

```text
Shadow → G-buffer → PS Draw / CS Dispatch → HDR
       → Skybox → Forward透明合成 → Bloom → 表示変換 → UI
```

## 位置を保存しないG-buffer

| ターゲット | 内容 | D3D12形式 | Vulkan形式 | B／画素 |
|---|---|---|---|---:|
| G0 | Base Color RGB・AO A | R8G8B8A8_UNORM_SRGB | R8G8B8A8_SRGB | 4 |
| G1 | Octahedral法線RG | R16G16_SNORM | R16G16_SNORM | 4 |
| G2 | Metallic R・Roughness G | R8G8_UNORM | R8G8_UNORM | 2 |
| G3 | HDR Emissive RGB | R11G11B10_FLOAT | B10G11R11_UFLOAT_PACK32 | 4 |
| Depth | 投影後の深度 | D32_FLOAT | D32_SFLOAT | 4 |

合計18 B／画素です。1280×720では16,588,800 bytes、約15.82 MiBです。HDR描画先、Shadow Map、Bloom、APIの割当アラインメントは別途必要です。この数値はターゲットの論理容量であり、実際の帯域や圧縮後の転送量ではありません。

Base Colorはシェーダから線形値を書き、sRGBターゲットが符号化します。`Load`時にRGBは線形へ戻り、AOのAlphaにはsRGB変換がかかりません。G3は非負のHDR発光を保持します。MetallicとRoughnessは8bitへ量子化されるため、Forwardとの微小な差が生じます。Roughnessは復号後も最低0.045へ制限します。

位置用ターゲット、ワールド／ビュー座標XYZ、追加の線形Depth画像は作りません。Depthをクリア値1、通常の0〜1深度、MSAAなしに固定し、背景画素は別途Skyboxへ渡します。

## 全方向の法線を2成分へ符号化する

Normal Mapと両面材質の処理を適用したワールド法線を、Octahedral方式でRGへ保存します。単にXYを保存して平方根でZを補う方式ではZの符号を失うため、全方向を扱う工房には折返しを含む表現を使います。[表現と精度の比較論文](https://jcgt.org/published/0003/02/01/paper-lowres.pdf)

```hlsl
float2 EncodeNormal(float3 n) {
    n /= dot(abs(n), 1.xxx);
    return n.z >= 0 ? n.xy : (1 - abs(n.yx)) * SignNotZero(n.xy);
}
float3 DecodeNormal(float2 e) {
    float3 n = float3(e, 1 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0 ? -t : t, n.y >= 0 ? -t : t);
    return SafeNormalize(n);
}
```

`SignNotZero`は0を正として扱います。極や折返し境界も試験し、CPUの10万方向のSNORM16往復では最大角度誤差0.00365度未満でした。G-buffer診断では符号化RGと復号した法線を切り替えて観察できます。

## Depthからワールド位置を再構築する

```hlsl
float3 ReconstructPosition(uint2 pixel, float depth) {
    float2 ndc = (float2(pixel) + .5) / screen.xy
               * float2(2, -2) + float2(-1, 1);
    float4 p = mul(float4(ndc, depth, 1), inverseViewProjection);
    return p.xyz / p.w;
}
```

画素中心に0.5を加え、画面の上から下へ増えるYをNDCへ変換します。row-majorの行列と行ベクトルの`mul`を維持します。Vulkanは負高さViewportを使うため、同じ式で復元できます。Depthを線形距離として扱わず、逆行列による変換後に`w`で除算する点が要点です。再構築した位置はライティング中の一時変数です。

## Workshopの読込と共有データ

[Workshopの制作仕様](Assets/Workshop.md)に合わせ、CPUローダーを追加しました。旧サンプルの単一モデル用ローダーは維持しています。

- 固有Primitive 62、材質16、画像40を保持し、配置がそれらを参照します。Standardは251,200三角形、Stressは同じ区画を3×3へ配置した2,260,800三角形です。これらはFullの数値で、OpaqueMaskでは透明カバーが除かれます。
- glTFの静的階層とTRSを評価し、右手系Y-upから左手系へ座標・法線・接線・巻き順・配置・ライトを変換します。非一様／負スケールなど、配布プロファイル外の入力は拒否します。
- PNGは用途ごとに色空間を指定してmipをCPUで作ります。色は線形空間で平均、Normalは再正規化、ORMは線形処理、MASKはAlpha Coverageを調整します。小さいmipでは画素数の制約から元のCoverageへ完全一致できません。
- 配布プロファイルのSamplerはLinear／Linear Mipmap Linear／Repeatに固定します。レンダラーと一致しない設定はCPU読込で拒否します。
- 形状と画像は配置ごとに再作成せず、共有GPUリソースを参照して個別Drawします。実行には生成済みglTF・PNG・DDS・JSONだけを使い、Blenderや再ベイクは不要です。

## PBR・IBL・Shadow・透明合成を接続する

GGX、Schlick Fresnel、correlated Smith、Lambertの`1/π`を既存サンプルから引き継ぎ、PS・CS・Forwardで同じBRDFを使います。環境はIrradiance、粗さ別Prefilter、BRDF LUTを参照し、AOは拡散IBLへ適用します。

ライトのglTF単位は点光源がcd、ディレクショナルライトがluxです。配布アセットのBlender換算に対応して強度を683で除算し、既存シェーダの相対的な放射量へ戻します。点光源は逆二乗減衰です。`range`なしは距離による打切りなしで、すべての点光源を評価します。

Shadow Mapは2048×2048です。シーン境界をライト空間へ変換し、2 mの余白を加えて投影範囲を固定します。遮蔽物の移動範囲±1.5 mを含み、カメラ移動で影の範囲は変わりません。Stressでは同じ解像度で広い範囲を覆うため、影の細かさも比較できます。MASKはG-bufferとShadowで同じAlpha Cutoffを使い、両面材質のカリング設定も揃えます。

Shadowはディレクショナルライトの直接光へ適用します。点光源のShadowやIBLの局所的な遮蔽は後続テーマです。

BLENDは別のForwardパスで、深度テストあり・深度書込みなし、配置の中心を基準に奥から手前へ並べて合成します。透明カバーはAlpha合成で、屈折や吸収を扱いません。発光モニターも静的な材質として描きます。

RGBA16FのHDR画像へSkyboxと透明を加え、半解像度のBloom抽出・9 tap横Blur・縦Blurを適用します。露出、Reinhard、sRGB出力は最後に一度だけ行います。Bloomの有効化は最終加算を切り替え、途中画像を常に診断できるようBlurパス自体は実行します。

## Forward比較とGPU時間

Forward診断はG-bufferを構築せず、同じ形状・材質・照明を直接HDRへ描きます。G-bufferの量子化差を含むため画素の完全一致を求めず、深度と見た目を比較します。PS／CS間ではG-bufferとDepthのバイト一致、およびHDRの絶対誤差`1e-3`＋相対誤差`2e-3`を試験します。

GPU時間はShadow、Geometry、Lighting、Sky、Transparent、Bloomの3パス、Display、Totalを表示します。GeometryはDeferredではG-buffer、Forwardでは不透明描画です。Timestampの結果はGPU完了後に読みます。CPU待機やPresent待ちはGPUパス時間とは別です。[D3D12のTimestamp](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)、[VulkanのTimestamp](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html)

Releaseの性能比較は1280×720、OpaqueMask、UIなし、VSyncなしです。60 framesの準備後、同じ経路のframe 0〜1199を1/60秒で評価し、1200 framesの平均・中央値・p95とCSVを保存します。最終端のframe 1200は対話表示で観察できます。Async Compute、ライト絞り込み、GPUインスタンシングは今回の計測へ加えません。

検証結果とGPU環境は[Deferred検証記録](DeferredVerification.md)へまとめます。

## Cyclesと比較するときの観察点

[Cyclesの参考画像](Assets/Workshop.md)は、同じ形状・配布テクスチャを使います。金属の曲面、窓や開口部の明るさ、床への接地、間仕切りの遮蔽を比較できます。Cyclesの点光源Shadow・間接光・材質モデルと、リアルタイムのIBL近似・Alpha合成には差があります。BeautyはAgXとdenoiseを使うため、画素一致を目指す画像として扱いません。

後続では、同じG-bufferを土台にTiled／Clusteredのライト絞り込みやSSAOを追加できます。Workshopの共有形状・配置は、レイトレーシングの加速構造やPrimitive単位のMeshletにも引き継ぐためのCPUデータ構成です。
