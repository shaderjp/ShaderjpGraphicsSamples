# 第21回：Visibility Bufferで可視性と材質評価を分ける

第19・20回では、G-bufferへBase Color、法線、Metallic、Roughness、AO、Emissiveを保存し、ライト候補を絞って照明を計算しました。第21回では、**画素に見えている配置と三角形のIDだけを保存し、材質の評価を後段へ移します。** Workshop、点光源列、Shadow Map、IBL、HDR、Bloomを引き継ぐDirect3D 12／Vulkanのサンプルです。

API側の接続は[Direct3D 12](Direct3D12/21_VisibilityBuffer.md)／[Vulkan](Vulkan/21_VisibilityBuffer.md)を参照してください。ライトカリングの復習は[第19・20回の共通章](LightCulling.md)、モデルの共有参照は[Workshopの制作仕様](Assets/Workshop.md)にまとめています。

## G-bufferと何が違うのか

従来は、形状を描くPixel Shaderがテクスチャを読み、Normal Mapを適用した法線や材質パラメータをG-bufferへ書きます。ライティングはこの加工済みの値を読みます。Visibility Bufferでは、ラスタライザーとDepth Testで可視面を決め、IDを介して元のIndex Buffer、Vertex Buffer、配置、材質を参照します。Compute Shaderが可視画素の属性を補間し、テクスチャを読み、照明を評価します。

この考え方の原典は[The Visibility Buffer: A Cache-Friendly Approach to Deferred Shading](https://jcgt.org/published/0002/02/04/paper.pdf)です。本サンプルは読みやすさを優先し、配置と三角形を別の32-bit整数で表し、材質ごとのWorklistは作らず一つのCompute Shaderで評価します。MSAA、材質分類、Indirect Dispatchは今回の範囲に含めません。

| 保存する情報 | 第19・20回のG-buffer | 第21回 |
|---|---|---|
| Base Color／AO | RGBA8、4 B | 後段で材質を評価 |
| 最終法線 | Octahedral RG16 SNORM、4 B | 頂点の法線・接線とNormal Mapから復元 |
| Metallic／Roughness | RG8、2 B | 後段でORMを評価 |
| Emissive | R11G11B10、4 B | 後段で発光テクスチャを評価 |
| 配置／三角形ID | なし | RG32 UINT、8 B |
| Depth | D32、4 B | D32、4 B |
| 合計 | 18 B／画素、1280×720で15.82 MiB | 12 B／画素、1280×720で10.55 MiB |

可視性用の画面サイズの画像は約33%小さくなります。この計算にHDR、Shadow、Bloom、ライトリスト、元の形状・材質Buffer、メモリアラインメントは含みません。属性を復元する仕事とBuffer参照が増えるため、容量が小さくなった割合をそのまま速度向上と扱うことはできません。

位置用のターゲットや重心座標の画像は作りません。ワールド位置はこれまでどおりDepthと逆ViewProjectionから一時変数として復元します。

## アルゴリズムの6段階

1. **共有形状と参照表をGPUへ置く。** 固有Primitiveの頂点とIndexを一つずつのBufferへ連結します。配置ごとにWorld／Normal Matrix、頂点・Indexの先頭、材質ID、三角形数を持ちます。Stressでも固有形状と画像を複製しません。通常描画とComputeが同じVB／IBを参照します。
2. **可視三角形のIDを描く。** OPAQUE／MASKを通常のIndexed TRIANGLESで描きます。Depthは0〜1、クリア値1、比較はLESSです。Pixel Shaderは配置IDと`SV_PrimitiveID`を整数ターゲットへ出します。OPAQUEは材質テクスチャを読みません。MASKだけはAlbedoのAlphaとFactor・Cutoffを使って穴を判定します。
3. **同じDepthからライトリストを作る。** 第19回のTiledを既定にします。16×16のXYタイル、24段のZスライスを使うClusteredと、全ライト評価にも切り替えられます。これは可視性の保存方式とは別の選択です。
4. **IDから元の三角形を読む。** 画素の配置IDからInstanceを選び、`indexOffset + primitiveID * 3`にある3つのIndexを読みます。IndexはPrimitive内のローカル番号なので、`vertexOffset`を加えて頂点を取得します。同じMeshを違う位置へ置いても、World Matrixは配置ごとに異なります。
5. **透視補正した属性とUV勾配を復元する。** 元の3頂点をWorld／ViewProjectionで変換し、画素中心に対応する重心座標を求めます。UV、法線、接線を補間し、UVの画面X／Y方向の勾配でテクスチャのmipを選びます。
6. **材質と照明を評価してHDRへ書く。** 共通のPBR、点光源、ディレクショナルライトのShadow、拡散／鏡面IBLを評価し、RGBA16Fへ書きます。SkyboxとBLENDのForward透明合成、Bloom、露出・Reinhard・sRGB、UI・Presentは従来の順序です。

```text
Shadow Map
→ Visibility ID + Depth
→ TileDepth → Tiled／Clusteredのライトリスト
→ 三角形参照・属性復元・材質・PBR（8×8 Compute）→ RGBA16F
→ Skybox → Forward透明合成 → Bloom → 表示変換 → UI
```

## IDの規約と背景・両面材質

`R32G32_UINT`のRへ`instanceIndex + 1`を、Gの下位31-bitへPrimitive内の三角形番号を保存します。Gの上位1-bitはラスタライザーが判定した裏面を記録します。クリアしたRが0なら背景で、配置表や形状Bufferへアクセスしません。配置順は一フレーム内で固定し、Scene変更時にはIDを描き直します。

```hlsl
return uint2(uint(screen.z), primitiveId | (front ? 0 : 0x80000000));
```

ここで`screen.z`はCPUが描画ごとに渡す配置番号です。三角形番号はDrawごとのローカル番号なので、それだけでシーン全体を識別することはできません。裏面のbitはNormal Mapを適用した最終法線の反転に使います。Shaderからの幾何判定で表裏を推測し直すことはしません。

MASKのAlpha判定とShadowのAlpha判定は従来の材質を使います。BLENDはIDへ記録せず、深度テストあり・深度書込みなしのForwardパスで奥から手前へ合成します。一画素に一つのIDを保存する仕組みだけでは、複数の透明面を保持できないためです。

## 重心座標を透視補正する

画面上の三角形に対する単純な線形補間では、透視投影のUVを正しく復元できません。この実装は3頂点の同次Clip座標`c0, c1, c2`と、画素中心のNDC座標`q`から、元の三角形に対する重み`λ0, λ1, λ2`を求めます。

```text
Σ λi (ci.xy − q * ci.w) = 0
Σ λi = 1
```

各頂点について`A = c0.xy − q*c0.w`、`B`、`C`を作り、2D外積で`n = (cross(B,C), cross(C,A), cross(A,B))`を計算すると、`λ = n / (n.x+n.y+n.z)`です。`UV = λ0*uv0 + λ1*uv1 + λ2*uv2`のように補間します。

頂点ごとに`clip.xy / clip.w`へ先に変換する式と異なり、頂点の`w=0`で除算しません。Near面をまたいでクリップされた三角形でも、IDが指す元の三角形に対して同じ射影関係が成り立ちます。縮退三角形はWorkshopのCPUローダーで拒否し、可視画素について分母が0にならないこととNaN／Infの不在を検証します。

## Computeからテクスチャを読むための勾配

従来のPixel Shaderは、近隣の2×2画素を使う暗黙の微分でmipを選べました。Computeで異なる三角形の隣接画素のUVを引くと、輪郭やUVの継ぎ目を巨大な勾配として誤認します。mip 0へ固定すると、遠い床や細かな模様がちらつきます。

本サンプルは重心座標の式を画面座標で微分します。画素をXへ一つ動かしたNDCの変化は`(2/width, 0)`、Yへは`(0, −2/height)`です。`λ = n / d`、`d = Σ n`なので、`∂λ = (∂n − λ*∂d) / d`を使えます。各頂点のUVへこの重みを掛け、`dUVdx`と`dUVdy`を作ります。

```hlsl
float2 uv = a.uv*bary.value.x + b.uv*bary.value.y + c.uv*bary.value.z;
float2 dx = a.uv*bary.dx.x + b.uv*bary.dx.y + c.uv*bary.dx.z;
float2 dy = a.uv*bary.dy.x + b.uv*bary.dy.y + c.uv*bary.dy.z;
float4 base = visibilityTextures[NonUniformResourceIndex(image)]
    .SampleGrad(materialSampler, uv, dx, dy);
```

Normal、ORM、Emissiveにも同じ勾配を使います。これは画素中心での解析的微分です。ラスタライザーのquadによる有限差分、G-bufferの8-bit／SNORM量子化と異なるため、第19・20回との画素一致は要求しません。同じ形状・Depth・BRDFを使い、その差を数値と画像で記録します。

## 法線・接線と画像の参照

IAの頂点は48 Bですが、VulkanのStorage Bufferへ`float3 / float2 / float3 / float4`のStructをそのまま宣言すると、std430の配置では64 Bのstrideになります。そこでGPUの読込型は3本の`float4`へ揃えます。最初のxyzが位置、wがU、次のxがV、yzwが法線、最後が接線です。同じ48 BのVBを参照でき、APIごとの頂点複製やScalar Block Layoutの追加は不要です。

法線は補間してNormal Matrixで変換し、接線はWorld Matrixで変換して法線に直交化します。接線のwと外積から従法線を作り、Normal Mapを適用して正規化します。裏面bitが立つ場合は最後に法線を反転します。Base ColorとEmissiveのsRGB画像はGPUのSRV／Image Viewで線形値へ戻します。ORMとNormalは線形です。材質Factor、法線強度、AO強度、HDR Emissiveを第19回と同じ規約で適用します。

画素ごとに材質が変わるため、テクスチャの配列番号はスレッド間で一様とは限りません。`NonUniformResourceIndex`でその性質を明示します。本サンプルは固有画像64個までの固定長Descriptor配列を使い、未使用の要素にも有効な画像を設定します。新しい画像ファイルや複製テクスチャは追加しません。Vulkanでは[非一様なSampled Image配列参照のfeature](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorIndexingFeatures.html)を確認して有効にします。

## 第19・20回と何を比較するのか

Tiled／Clusteredは「どのライトを調べるか」、Visibility Bufferは「可視面と材質をどう保存・評価するか」を変えます。両者は組み合わせられます。第21回の既定をTiledにすることで、第19回との比較ではライトリストを揃え、第20回との差にはZ分割の仕事が含まれることを明示します。さらに第21回をClusteredへ切り替えた測定も保存します。

Release・1280×720・OpaqueMask・UI非表示・VSyncなし、同じ固定Hashの点光源、半径1.6 m、強度0.4、同じ20秒経路を使います。60フレームの準備後、固定1/60秒の1200フレームを評価します。Standardは264点光源、Stressは2,376点光源です。Shadow、PCF、IBL、Bloom、露出1、環境強度0.8も揃えます。

GPU Timestampの`geometry`は、第19・20回ではG-buffer、第21回ではID描画です。第21回の`lighting`には三角形参照、属性復元、材質テクスチャ読込も含みます。軽くなったIDパスだけで速度を判断せず、`tile-depth`、`cull`とフレーム全体の平均・中央値・p95を見ます。CPU更新、APIコマンド記録、Present待機はこのGPU時間に含みません。

実測表・機材・数値検証は[検証記録](VisibilityVerification.md)、再実行できるCSV／JSONは[測定データ](Measurements/Visibility/)にまとめます。帯域、頂点参照のキャッシュ、細かな三角形、画面内の材質の混在、GPUによって効果は変わります。常にG-bufferより速いという一般化はしません。

### 同じ経路の実測結果

RTX 4070、上記条件の全体平均GPU時間です。第19・20回も今回のビルドで再計測しました。

| API／Scene | 第19回 Tiled G-buffer | 第20回 Clustered G-buffer | 第21回 Tiled Visibility | 第21回 Clustered Visibility |
|---|---:|---:|---:|---:|
| Direct3D12／Standard | 1.108 ms | 1.136 ms | 1.002 ms | 1.074 ms |
| Direct3D12／Stress | 4.234 ms | 4.166 ms | 3.848 ms | 3.880 ms |
| Vulkan／Standard | 1.205 ms | 4.619 ms | 0.967 ms | 3.244 ms |
| Vulkan／Stress | 2.356 ms | 5.297 ms | 2.067 ms | 5.475 ms |

この測定では、Tiledの第19回から第21回への全体時間は、Direct3D 12でStandard約9.5%・Stress約9.1%、VulkanでStandard約19.8%・Stress約12.3%短くなりました。各条件でIDパスはG-buffer構築より短く、属性復元を加えたlightingは長くなっています。

Clustered同士では、VulkanのStressが第20回5.297 msに対して第21回5.475 msで、約3.4%長くなりました。Standardでは4.619 msから3.244 msへ短縮しています。候補の絞込みだけでなく、セル管理・形状参照・材質評価と実装全体の時間を見る必要があります。単一機材のこの測定から、VisibilityやClusteredの優劣を一般化しません。

ID描画が軽くなっても、属性復元はlighting側へ移ります。また、共有VB／IBへの連結でResourceの割当単位も変わるため、画像容量だけの効果を分離した実験ではありません。パス別平均、全体の中央値・p95、画質差の数値は[検証記録](VisibilityVerification.md)に掲載しています。

## 操作と観察点

既定はStandard／Hero／OpaqueMask／Tiledです。右ドラッグでカメラ回転、WASDで前後左右、Q／Eで上下、Shiftで加速、Resetで標準へ戻します。Scene、Preset、固定カメラ、20秒経路、ライト数、range、Tiled／Clustered／All、容量、Shadow、IBL、Bloomはパネルで変更できます。

ViewのInstance IDは配置ごと、Triangle IDは各Primitive内の三角形ごとに色を変えます。Barycentricsは3頂点の重みをRGBへ出し、UV gradientはX／Y勾配の長さを32倍してRGへ表示します。これらの診断表示ではBloomを無効にします。材質・法線・Depthの既存表示も、保存値を読む代わりに復元した結果を表示します。

Forward診断は元の材質をPixel Shaderで評価する経路です。IDパスと復元を省き、材質と投影を確認できます。ライト候補の診断はCandidates／Z slice／Overflowで確認し、容量を1へ下げると全ライト評価への復帰を観察できます。

## 今回の範囲と次の拡張

通常のIndexed DrawでIDを出すため、Mesh Shaderやレイトレーシング対応GPUは必要ありません。VulkanのPrimitive IDには`geometryShader`のfeatureを有効にしますが、Geometry Shaderのパスは作りません。Shader Model 6.6、Vulkan 1.3、既存のAPI・座標・binding規約を維持します。

Meshletから出す三角形を同じIDへ対応させる場合は、Meshlet内番号から元のPrimitive内番号への表が必要になります。レイトレーシングで得るヒット情報との共用でも、配置・Primitive・三角形の識別と材質参照を揃えることが重要です。今回の実装は、その接続を考えやすい配置と固有形状の分離を維持します。
