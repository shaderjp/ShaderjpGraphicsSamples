# Direct3D 12 第21回：Visibility Bufferで材質評価を後段へ移す

第19回のTiledライトカリングとWorkshopを引き継ぎ、G-bufferの4枚を**配置・三角形IDの整数ターゲット1枚**へ置き換えます。Compute Shaderで元の頂点と材質を読み、UV・法線・接線を復元してPBRを評価します。アルゴリズム、勾配の式と第19・20回との比較は[共通章](../VisibilityBuffer.md)、実測と数値検証は[検証記録](../VisibilityVerification.md)を参照してください。

![Visibility Bufferの完成表示](Images/21_VisibilityBuffer-base.png)

1280×720、Standard／Hero／OpaqueMask、264点光源、Tiled、Shadow・PCF・IBL・Bloom有効、露出1、環境強度0.8です。G-bufferへ位置や重心座標を保存せず、可視性用の画像はID 8 B＋Depth 4 Bの12 B／画素です。

## ビルドと実行

VS 2026／v145、SDK 10.0.26100.0、C++20、x64、Agility SDK 1.619.6とNuGet DXC 1.9.2609.5を使います。Shader Model 6.6／HLSL 2021をオフラインでコンパイルし、配布済みWorkshopのglTF・PNG・DDS・JSONだけで実行できます。

```powershell
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\21_VisibilityBuffer\Direct3D12_21_VisibilityBuffer.vcxproj -p:Configuration=Release -p:Platform=x64
bin\x64\Release\Direct3D12_21_VisibilityBuffer\Direct3D12_21_VisibilityBuffer.exe
```

## アルゴリズムの流れ

1. 固有頂点・Indexを共有Bufferへ連結し、配置表からWorld Matrix、形状の先頭、材質を参照する。
2. Indexed Drawで最前面の配置IDと三角形IDをDepthとともに記録する。MASKだけはAlpha判定する。
3. 同じDepthから第19回のライトリストを作る。
4. 可視画素のIDが示す3頂点を読み、同次座標から透視補正した重心座標とUV勾配を計算する。
5. `SampleGrad`で材質を評価し、同じBRDF・ライト・Shadow・IBLでHDRへ書く。
6. Skybox、Forward透明合成、Bloom、表示変換を行う。

Tiled／Clusteredはライトの選び方、Visibility Bufferは可視面の保存方法です。第21回は既定をTiledに揃え、第19回との比較でG-bufferを置き換えた効果を調べます。パネルと`--light-culling cluster`で第20回のリストにも接続できます。

## リソースと接続

`Sample::CreateScene`は固有形状を連結し、同じ頂点・IndexのResourceからIAのVBV／IBVとComputeのStructuredBuffer SRVを作ります。Indexは各Primitiveのローカル番号を維持します。各DrawのViewを形状の先頭へずらし、Computeでは参照表の先頭を加算します。配置別に形状をコピーしません。

| リソース | 形式・サイズ | 用途 |
|---|---|---|
| Visibility | `DXGI_FORMAT_R32G32_UINT` | RTVで書き、整数SRVで`Load`する |
| Depth | Resourceは`R32_TYPELESS`、DSVは`D32_FLOAT`、SRVは`R32_FLOAT` | 深度テスト、位置復元、ライトカリング |
| Vertex／Index | 頂点48 B、Indexはuint32 | IAとComputeで同じResourceを参照 |
| Instance | 144 B／配置 | World／Normal Matrix、先頭と材質・三角形数 |
| Material | 80 B／材質 | Factor、画像ID、Alpha条件 |
| 画像Descriptor配列 | 固有画像64個まで | 元のsRGB／線形SRVを再利用 |
| HDR | `R16G16B16A16_FLOAT` | ComputeのUAVから後続RTV・SRVへ |

`b0／t0／s0`の規約を維持し、既存の環境・Shadow・ライト・Depthと同じRoot Signatureへ、`t17..t20`の形状・配置・材質と、`t21..t84`の画像配列を加えます。旧G-buffer用の未使用bindingは予約し、対応する画面サイズの材質画像は作りません。固定長配列の未使用Descriptorにも有効な画像を設定します。

既存サンプルと同様、CPU更新する定数と配置表はGPU完了を待ってから書き換えます。VB／IBは既存と同じUpload Heap配置です。計測はこの条件を含むサンプル同士の比較で、Device Local化など別の変更は混ぜません。

## ラスタパスのシェーダ

[VisibilityVertex.hlsl](../../Shaders/Visibility/VisibilityVertex.hlsl)は位置とUVだけを出し、[VisibilityPixel.hlsl](../../Shaders/Visibility/VisibilityPixel.hlsl)は次の2整数を返します。

```hlsl
uint2 PSMain(VisibilityVertex i, uint primitiveId : SV_PrimitiveID,
             bool front : SV_IsFrontFace) : SV_Target0
{
    if (emissiveFactor.w == 1)
        clip(baseTexture.Sample(materialSampler, i.uv).a * baseColorFactor.a - screen.w);
    return uint2(uint(screen.z), primitiveId | (front ? 0 : 0x80000000));
}
```

Rの0は背景、配置番号は1から始めます。Gの上位bitは裏面、残りが三角形番号です。MASKではPixel ShaderによるAlpha判定を維持します。OPAQUEではAlbedo・Normal・ORM・Emissiveを読まずにIDを記録します。

## Computeの属性復元と材質評価

GPU読込型を3本の`float4`へ揃え、位置・UV・法線・接線へ戻します。これはVulkanのStorage Bufferの配置規則と48 BのIA頂点を一致させるためです。Direct3D 12でも同じHLSLと同じVBを使います。

[VisibilityRead.hlsli](../../Shaders/Visibility/VisibilityRead.hlsli)がIDの読込からPBRまでを実装します。配置表のIndex先頭と三角形番号から3つのIndexを読み、頂点先頭を加えます。World／ViewProjectionへ変換したClip座標と画素中心から透視補正した重みを計算します。

重心座標の解析的微分を使うため、別の三角形に属する隣接画素のUVは参照しません。全画像を`SampleGrad`で読み、`NonUniformResourceIndex`で画素間の異なる材質を明示します。法線・接線の変換とNormal Map、裏面反転を適用した後、共通の`ShadeSurface`で照明を評価します。

[VisibilityCompute.hlsl](../../Shaders/Visibility/VisibilityCompute.hlsl)は8×8のThread Groupを使い、`Dispatch((width+7)/8, (height+7)/8, 1)`します。画面外のスレッドは最初に戻り、背景IDはBuffer参照前に戻ります。

## 同期

IDは`RENDER_TARGET → ALL_SHADER_RESOURCE`、Depthは`DEPTH_WRITE → DEPTH_READ | ALL_SHADER_RESOURCE`へ遷移します。IDとDepthを使うライトリスト生成・材質復元は、同じDirect Queueのコマンドへ記録します。

HDRは`UNORDERED_ACCESS`でComputeから書き、Skyboxと透明合成の前に`RENDER_TARGET`へ戻します。この遷移で書込みと後続描画を同期し、Bloomの前にはSRVへ渡します。ライトリストのUAVからSRVへの遷移は第19回のコードを維持します。

## 比較画像と操作

![配置ID](Images/21_VisibilityBuffer-instance-id.png)

Instance IDは配置単位の色です。Stressの同じ設備でも配置IDは異なり、元のVertex／Indexと画像は共有します。

![三角形ID](Images/21_VisibilityBuffer-triangle-id.png)

Triangle IDはPrimitive内の番号で色を付けます。配置を識別する値ではないため、異なるPrimitiveで同じ色が現れます。

![透視補正した重心座標](Images/21_VisibilityBuffer-barycentrics.png)

RGBは元の3頂点の重みです。細い羽根と面取りを拡大し、属性を元の三角形から再計算することを確かめます。

![UV勾配](Images/21_VisibilityBuffer-uv-gradient.png)

RGは画面X／Y方向のUV勾配の長さを32倍して表示します。値が1を超える部分は表示上だけ飽和します。テクスチャのmip選択に使う値は飽和させません。

右ドラッグ、WASD、Q／E、Shift、RとResetで視点を操作できます。View、Scene、Preset、Shadow、IBL、Bloom、ライト数、候補リスト、容量を切り替えます。Forward診断ではPixel Shaderの従来材質と見比べられます。

```powershell
Direct3D12_21_VisibilityBuffer.exe --shading barycentrics --no-ui
Direct3D12_21_VisibilityBuffer.exe --workshop-scene Stress --camera StressHero --no-ui
Direct3D12_21_VisibilityBuffer.exe --light-culling cluster --no-ui
Direct3D12_21_VisibilityBuffer.exe --list-capacity 1 --light-view overflow --no-ui
```

## 性能比較と検証

![共有形状を反復配置したStress](Images/21_VisibilityBuffer-stress.png)

Stress／StressHero、OpaqueMask、2,376点光源です。設備を反復しても固有VB／IBと画像は共有し、配置表とIDで見えている形状を識別します。

![Forwardパスで合成する透明カバー](Images/21_VisibilityBuffer-full.png)

Standard／Transparency、Full。カバーはVisibility Bufferへ書かず、復元した不透明面のHDRとDepthを使うForwardパスで合成します。

RTX 4070での全体平均GPU時間です。Release・1280×720・OpaqueMask・UI非表示、同じ20秒経路と1200フレームで測定しました。

| Scene | 第19回 | 第20回 | 第21回 Tiled | 第21回 Clustered |
|---|---:|---:|---:|---:|
| Standard | 1.108 ms | 1.136 ms | 1.002 ms | 1.074 ms |
| Stress | 4.234 ms | 4.166 ms | 3.848 ms | 3.880 ms |

同じ機材で第19・20・21回を再計測しました。[共通章の比較条件](../VisibilityBuffer.md)、[結果と限界](../VisibilityVerification.md)を確認してください。IDパスの`geometry`と、材質復元を含む`lighting`を分け、最終的にはフレーム全体で判断します。

```powershell
& Tools\TestVisibility.ps1 -Configuration Debug
& Tools\TestVisibility.ps1 -Configuration Release -CaptureDocs
& Tools\TestVisibility.ps1 -Configuration Release -BenchmarkOnly
& Tools\TestSamples.ps1 -Configuration Debug -Topics 21_VisibilityBuffer
& Tools\TestCameraNavigation.ps1 -Configuration Debug -Topics 21_VisibilityBuffer
```

整数IDの範囲、共有参照、Depthの一致、Near面のクリップ、重心座標とUV勾配の独立したCPU計算、MASK／両面／BLEND、容量超過、641×479、カメラ・Reset・リサイズ・最小化復帰・終了を検証します。画像やSourceArtの正本を変更せず、実GPUから新しい1280×720画像を撮影します。
