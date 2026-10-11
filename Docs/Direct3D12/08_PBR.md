# Direct3D12 08: Metallic-Roughness PBR入門

07の経験的な鏡面反射をGGXのマイクロファセット反射へ進め、同じShader BallにNormal・ORM・Emissiveを追加します。1本の方向光で材質の違いを確認します。

![Direct3D12の実GPU描画](Images/08_PBR.png)

## ビルドと実行

Visual Studio 2026／v145、Windows SDK 10.0.26100.0、C++20、x64です。Agility SDK 1.619.6／DXC 1.9.2609.5をNuGetで復元します。 nlohmann/jsonは06と同じ固定Submoduleを使用します。

```powershell
git submodule update --init --recursive
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\08_PBR\Direct3D12_08_PBR.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_08_PBR\Direct3D12_08_PBR.exe
```

VSでは`Direct3D12_08_PBR`をスタートアッププロジェクトにします。Vulkan SDKなしで単体ビルドできます。アセットとシェーダは実行ファイル基準で読みます。

## 操作と比較

| キー | CLI | 表示 |
|---|---|---|
| 1 | `--shading pbr` | PBR。既定値 |
| 2 | `--shading albedo` | Base Color |
| 3 | `--shading normal` | Normal Map適用後のワールド法線を色で表示 |
| 4 | `--shading metallic` | Metallic |
| 5 | `--shading roughness` | Roughness |
| 6 | `--shading ao` | strength適用後のAO |
| 7 | `--shading emissive` | Emissive |
| 8 | `--shading pbr-flat` | Normal MapなしのPBR |
| Space | `--rotate-light`で回転開始 | 方向光の回転を開始／停止 |

![Metallicの確認表示・実GPU](Images/08_PBR-metallic.png)

![Roughnessの確認表示・実GPU](Images/08_PBR-roughness.png)

確認表示もsRGB描画先を使うので、例えば線形値0.5のスカラーは約188/255の明るさで見えます。画像として保存されたRGB値をそのまま線形値と解釈しないでください。

## glTFのPBR材質を読む

`LoadGltfPbr`はShaderBall_PBR.gltfを既定値に使います。POSITION／NORMAL／UV0にTANGENTを加え、CPUローダーでZとtangent.wを反転済みです。glTFのUVをOBJのようにV反転する処理はありません。

材質のtexture indexから画像をたどり、Albedo、Normal、ORM、Emissiveの4 PNGを読みます。Metallic／Roughness Factor、Normal Scale、Occlusion Strength、Emissive Factorも読みます。ORMはR=AO、G=Roughness、B=Metallicです。occlusionTextureとmetallicRoughnessTextureが同じPNGを指し、全画像が同じSampler・UV0を使う教材用プロフィールに限定します。

法線・接線の数、接線の単位長と直交、三角形内のWの一致、画像の参照、係数の範囲を確認します。未対応データにはファイル名とJSON Pointerを含むエラーを出します。06／07の`LoadGltfAlbedo`は追加画像を読みません。

## GPUへ4画像と接線を渡す

[Sample.cpp](../../Samples/Direct3D12/08_PBR/Sample.cpp)の`CreatePipeline`／`CreateScene`／`LoadTexture`が中心です。位置0、UV12、法線20、接線32の48-byte PbrVertexをVertex Bufferへ転送し、Indexは32-bitです。

320-byteのLightingConstantsを、256-byte境界へ切り上げた512-byteのUpload Heapへ置きます。MapしたCPUアドレスを毎フレーム更新し、GPUアドレスをSetGraphicsRootConstantBufferViewで渡します。

Root Signatureはb0のRoot CBV、t0〜t3を連続させた4個のSRV Descriptor Table、s0のStatic Samplerです。Shader-visible Heapにも4個のSRVを作り、0=Albedo、1=Normal、2=ORM、3=Emissiveの順に並べます。

各画像はWICで展開し、行のパディングをGetCopyableFootprintsで処理してUpload HeapからDefault Heapへコピーします。COPY_DESTからPIXEL_SHADER_RESOURCEへ遷移して、Fenceで完了を待ちます。Albedo／EmissiveのResourceとSRVはR8G8B8A8_UNORM_SRGB、Normal／ORMはR8G8B8A8_UNORMです。

すべてmip 0だけを転送し、SamplerのLOD範囲を0にします。描画時には4画像を同時に参照します。API側の転送・View・Descriptor・同期はこのプロジェクトに明示しています。

## PBRシェーダの順序

[Pixel.hlsl](../../Shaders/08_PBR/Pixel.hlsl)を両APIで共有します。SM6.6／HLSL 2021でDXILとSPIR-Vへ事前コンパイルします。

1. sRGBのBase ColorとEmissiveを線形値としてサンプルし、各Factorを掛けます。
2. 線形のORMからMetallic、Roughness、AOを取り出します。
3. 補間したNとTを直交化し、`B=cross(N,T)*w`で接線基底を作ります。Normal Scaleを適用した接線空間法線をワールドへ変換します。
4. F0を非金属の0.04と金属のBase Colorから補間します。
5. GGXのD、SchlickのF、Smith GGXのVisibilityで鏡面反射を評価し、`(1-F)*(1-metallic)*baseColor/π`を拡散BRDFとして配分します。
6. 直接光にNoLと光の色を掛けます。AOは一定拡散環境項だけ、Emissiveは発光項として加えます。
7. Reinhardで範囲を圧縮し、sRGB描画先で出力をエンコードします。

詳しい式と入力値の意味は[07→08の共通解説](../LightingAndPBR.md)を参照してください。

## Depth Bufferとライティング

`CreateDepth`でウィンドウサイズの`DXGI_FORMAT_D32_FLOAT`を作ります。`ALLOW_DEPTH_STENCIL`、初期状態`DEPTH_WRITE`、最適化クリア値1.0を指定し、DSV HeapにViewを作ります。`OnRender`でRTVとDSVを`OMSetRenderTargets`へ渡し、毎フレーム`ClearDepthStencilView`で1.0へクリアします。

PSOのDepthEnableはTRUE、DepthWriteMaskはALL、DepthFuncはLESSです。遠い面を先に描いても、より小さい深度を持つ手前の面が通り、その深度を保存します。近い面を先に描いた場合には、後の遠い面が比較に失敗します。リサイズ時はFenceでGPU完了を待ち、古い深度リソースを解放して新しい大きさで作り直します。

カメラと投影は左手系、深度範囲は0〜1です。球と台座の前後関係はこの比較で決まり、Pixel Shaderの明るさや反射モデルは比較値を変えません。Normal Mapを使う08も、変わるのは反射計算用の法線で、輪郭や深度を動かしません。Shadow Mapが光から見た遮蔽を判定する処理は別のテーマです。

## 検証と次の回

```powershell
& .\bin\x64\Debug\Direct3D12_08_PBR\Direct3D12_08_PBR.exe --shading pbr-flat --frames 48 --test-window --screenshot ".\.cache\Direct3D12-08.png" --log ".\.cache\Direct3D12-08.log"
.\Tools\TestLoaders.ps1
.\Tools\TestSamples.ps1 -Topics 08_PBR
.\Tools\TestLighting.ps1
```

`--model`でPBR用プロフィールに合うglTFへ差し替えられます。`--texture`はBase Colorだけを置換します。Normal／ORM／EmissiveはglTF内の参照を使います。指定した相対パスは起動時の作業ディレクトリ、glTF内のURIはglTFの場所が基準です。

DebugではD3D12 Debug Layerを有効にして検証しています。CPUの材質試験、実GPUの既知画素試験、リサイズ、最小化復帰、終了を確認し、[検証記録](../Verification.md)にバージョンを残しています。添付画像はこのAPIによる実GPU描画です。

08は直接光のPBR入門です。一定拡散環境項は小さく、金属の環境反射はまだ加えていません。そのため金属領域は鏡面反射の方向以外で暗く見えます。Normal Mapで球の表面模様が出ること、金属のハイライトがBase Colorの色になること、粗さが異なる領域の反射、Emissiveの赤い模様を確認します。Blender参考画像とは光と表示変換の条件が異なります。

次はIBL・環境Cubemap、Shadow Map、複数光源、Deferred Renderingなどへ拡張できます。

前：[07_Lighting](07_Lighting.md)。 別API：[Vulkan 08](../Vulkan/08_PBR.md)。
