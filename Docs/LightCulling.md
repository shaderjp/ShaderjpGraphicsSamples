# 多数の点光源を扱うTile-based DeferredとClustered Shading

第17・18回では、G-bufferから復元した表面に対して全点光源を順に評価しました。第19回では画面をタイルへ、第20回ではタイルをさらに奥行きへ分け、表面へ届く可能性のあるライトだけを評価します。WorkshopのPBR、IBL、ディレクショナルライトのShadow Map、HDR、Bloom、Forward透明合成を引き継ぎます。

| 回 | 題材 | Direct3D 12 | Vulkan |
|---|---|---|---|
| 19 | Tile-based Deferred Rendering | [記事](Direct3D12/19_TiledDeferred.md) | [記事](Vulkan/19_TiledDeferred.md) |
| 20 | Clustered Shading | [記事](Direct3D12/20_ClusteredShading.md) | [記事](Vulkan/20_ClusteredShading.md) |

## ライトを増やして同じ画を比較する

既存の8／72個の点光源に加え、各設備区画へ256個の点光源を配置します。Standardは264個、Stressは2,376個です。UIでは追加数を0〜512個／区画へ変えられ、Stressの最大は4,680個になります。固定Hashから位置を作り、暖色、青、緑、黄の4色を繰り返します。比較する方式で位置・色・強度を変えません。

追加ライトの既定半径は1.6 m、相対的な線形RGB強度は0.4です。逆二乗減衰に、glTFの有限`range`と同じ滑らかな打切りを適用します。

```hlsl
float attenuation = 1 / max(distanceSquared, 1e-4);
attenuation *= pow(saturate(1 - pow(distance / range, 4)), 2);
```

半径の外で寄与が0になることが、球によるライトの除外を成立させます。これは「無限に届く光を見た目で切り捨てる」処理ではありません。既存glTFの`range`なしの点光源は無限の影響範囲を持つため、可視セルのリストへ必ず残します。新しいライトにはShadowを加えず、ディレクショナルライトのShadowを維持します。

`All lights`、`Tiled`、`Clustered`は同じ実行ファイル内で切り替えられます。第19・20回は独立したプロジェクトですが、既定モード以外のGPU処理と共有HLSLは揃えています。点光源の数だけを変える比較、同じライト列で絞り込み方式だけを変える比較を分けて観察してください。

既存のCycles参考画像は配布glTFの8／72個の点光源を使っています。追加ライトを0個へ戻すと、その照明条件に近づけて比較できます。今回の追加ライトはサンプルで生成し、WorkshopのBlender正本やglTFは変更しません。

## G-bufferとBRDFを維持する

Base Color＋AO、Octahedral法線RG、Metallic＋Roughness、HDR EmissiveとDepthの構成は[Deferred共通章](DeferredRendering.md)と同じです。合計18 B／画素で、位置用ターゲットは追加しません。位置はDepthと逆ViewProjectionから復元し、GGX、Schlick、Smith、拡散・鏡面IBLを同じ関数で評価します。

追加するのは、タイルの深度範囲とスライスマスク、セルごとのライト数と容量超過フラグ、ライトIDのリストです。ライトの球中心は判定時にView空間へ変換します。これは少数のライトのBufferとカメラ定数であり、画素ごとの位置画像ではありません。

```text
Shadow → G-buffer
→ タイルの最小／最大View Zとスライスマスク
→ タイル／クラスターのライトリスト
→ Compute Lighting → Skybox → Forward透明合成
→ Bloom → 露出・Reinhard・sRGB → UI
```

## 第19回：16×16画素のタイル

1280×720では80×45、3,600タイルです。深度パスは16×16のThread Groupを使い、画面と同じ位置のG-buffer Depthを読みます。背景のクリア値1は除外します。

透視投影のDepthを距離と誤認しないよう、Projectionの係数A／Bから正のView Zを求めます。左手系、通常のDepth 0〜1の規約を維持しています。

```hlsl
float z = projectionB / (depth - projectionA);
InterlockedMin(nearestZ, asuint(z));
InterlockedMax(farthestZ, asuint(z));
```

正の浮動小数点値は、符号なし整数へbitcastして大小を比較できます。Group共有変数を初期化した後と、全画素を処理した後に`GroupMemoryBarrierWithGroupSync`を置きます。画像外のスレッドもバリアへ参加し、画像の読取りだけを境界判定で避けます。背景だけのタイルは深度範囲0、スライスマスク0を保存します。

次のパスは1タイルを64スレッドで担当します。各スレッドが64個おきのライトを調べ、候補をリストへ追加します。タイルの画面四辺を通る平面と、最小／最大View Zの2面が判定範囲です。

```hlsl
float leftDistance = center.x - leftSlope * center.z;
bool insideLeft = leftDistance >= -radius * sqrt(1 + leftSlope * leftSlope);
```

平面法線を毎回正規化する代わりに、球半径へ法線の長さを掛けます。他の3面とZ範囲も判定し、完全に外側の球を除外します。浮動小数点の境界を保守的に扱う小さな余白を加えています。

近い壁と遠い床が同じタイルへ入ると、最小と最大の間が広くなります。その間に実際の表面がなくても、そこにあるライトを候補へ残すことが、次のクラスターを考える入口です。

## 第20回：奥行きを24段へ分ける

同じXYタイルを、NearからFarまでの対数スライスへ分けます。

```hlsl
uint slice = min(uint(floor(log2(max(z, nearPlane) / nearPlane)
                           * 24 / log2(farPlane / nearPlane))), 23);
float zNear = nearPlane * pow(farPlane / nearPlane, slice / 24.0);
float zFar  = nearPlane * pow(farPlane / nearPlane, (slice + 1) / 24.0);
```

スライス選択ではNear未満とFar端をクランプします。Near／Farは現在のカメラから取り、CPUとHLSLで一致させます。整数のセル番号は`(slice * tilesY + tileY) * tilesX + tileX`です。

深度パスはタイルごとに24-bitのマスクも作ります。不透明物の画素が属するスライスのbitを`InterlockedOr`で立てるため、別のClearや画素ごとのクラスタID画像は不要です。ライト判定ではマスクが0のクラスターを処理せず、Headerを0へ上書きします。

有効なクラスターはスライスのZ範囲と、タイルで観測した最小／最大Zを交差させます。奥行きの空いた端を除きながら、同じタイル内の近い表面と遠い表面へ別々のライトリストを渡せます。

このサンプルは画面XYとView Zによる分割です。法線方向による追加分類、法線コーンによるライトの背面除外、ライト階層の走査は加えていません。[Clustered Deferred and Forward Shading](https://diglib.eg.org/items/6342d4d6-5220-4376-a5c6-a153058f4a3c)は、DeferredとForwardの両方に加え、法線を用いる拡張も扱う元論文です。[Tiled Shading](https://research.chalmers.se/en/publication/152886)も参照してください。

## 固定長リストと容量超過

1セルに256個のライトIDを予約します。Headerは`uint2(candidateCount, overflow)`、Indexは`uint`です。グループの共有カウンターへ`InterlockedAdd`し、返された番号が容量以内ならIDを書きます。容量を超えても範囲外へ書きません。

容量超過を「ライト数を256へ切り詰める」処理にはしません。そのセルのシェーディングを全ライトのループへ戻し、光が欠けないようにします。UIと赤い診断表示で容量超過を確認でき、`--list-capacity 1`で意図的に発生させられます。

固定長リストは、アドレス計算と限界を読みやすくするための選択です。1280×720でHeaderとIndexの論理容量は、Tiledが3,715,200 bytes（約3.54 MiB）、Clusteredが89,164,800 bytes（約85.03 MiB）です。タイル深度とマスクは合計43,200 bytes、HDRやアラインメントは別です。未使用セルのIndex領域も予約します。Vulkanでは作成時に`maxStorageBufferRange`を確認します。

より小さいメモリを目指すなら、可視セルの圧縮、Count／Prefix Sum／Fillによる可変長リスト、ライト側からの登録へ進めます。それらの生成コストも計測へ加える必要があります。

## 不透明物と透明物のリストを分ける理由

最小／最大ZとスライスマスクはOPAQUE／MASKのDepthから作っています。透明カバーの表面が、同じ位置の不透明Depthと異なるクラスターへ属することもあります。そのため、BLENDのForwardパスとForward診断では全点光源を評価します。材質とAlphaの規約、奥から手前への並べ替え、深度テストあり・深度書込みなしを維持します。

透明物もClusteredで絞る拡張では、不透明Depthだけで有効セルを決めず、透明物の範囲も登録する必要があります。

## 画像と計測で確かめる

`Candidates`は表面が参照する候補数、`Z slice`は奥行きスライス、`Overflow`は容量超過を表示します。候補数の色は0〜128以上を青から赤へ変化させ、16画素の境界を暗い線で示します。UIの平均は有効セル単位で、画素数で重み付けした平均ではありません。

GPU時間には従来の各パスに加え、`tile-depth`と`cull`を分けて追加しました。ライティング時間だけでなく、リスト作成を含む`total`で比較します。統計用HeaderのGPU読戻しとCPU集計は`total`の外です。読戻しは既存のGPU完了待機の後に読み、CPUの待機時間をGPUの処理時間と混ぜません。

Release、1280×720、OpaqueMask、UIなし、VSyncなし、同じ20秒のカメラ経路を固定1/60秒で評価します。60 framesの準備後に1200 framesを測り、平均・中央値・p95とCSVを保存します。画像や速度を比較するときは、Scene、ライト数、半径、強度、カメラとShader版を揃えてください。[検証記録](LightCullingVerification.md)へ実測結果をまとめています。

全ライトとのHDR比較に加え、GPUから読み戻したリストを独立したCPUの球／平面判定でも調べます。有限ライトの欠落、`range`なしのライト、IDの範囲、重複、容量超過、奇数サイズ、Near付近とカメラの背後に中心があるライトを確認します。

これらのサンプルは同じGraphics／Compute Queueで処理します。Async Compute、点光源のShadow、多数ライトのレイトレーシング、メッシュレットによる形状カリングは、照明候補の絞り込みとは別に比較する後続テーマです。


## 今回のGPU時間

Release、1280×720、OpaqueMask、UI／VSyncなし。追加256個／区画、半径1.6 m、強度0.4、Shadow・PCF・IBL・Bloom有効。60 framesの準備後、同じ20秒の経路のframe 0〜1199を固定1/60秒で評価しました。各条件1200 framesです。リスト生成はTileDepth＋CullLights、TotalはShadowから表示処理までのGPU時間です。統計読戻しとCPU待機は含みません。

表はGPU全体の平均時間（ms）です。

| API | Scene | All | Tiled | Clustered |
|---|---|---:|---:|---:|
| Direct3D12 | Standard | 2.202 | 1.065 | 1.112 |
| Direct3D12 | Stress | 19.811 | 4.110 | 4.133 |
| Vulkan | Standard | 2.300 | 1.230 | 3.292 |
| Vulkan | Stress | 18.284 | 2.455 | 4.475 |

この測定では、D3D12のStressで全体平均はAll 19.811 ms、Tiled 4.110 ms、Clustered 4.133 msでした。VulkanはTiledが両Sceneで短く、StandardのClusteredはAllより長くなりました。候補数が減っても、生成とメモリ参照、画素間の処理の違いにコストがあります。これらはこのGPU・Shader・シーン条件の結果で、Clusteredが常に速いという結論にはしません。

詳細なパス別の値、平均・中央値・p95、JSON／CSVは[検証記録](LightCullingVerification.md)から確認できます。
