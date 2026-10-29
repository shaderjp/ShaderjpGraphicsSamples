# Direct3D12 06: glTFモデルのAlbedo表示

Shader BallのglTFをCPUで読み込み、Direct3D12のVertex／Index Buffer、MVP定数バッファ、AlbedoのSRVで描画します。照明を計算せずBase Colorだけを表示します。

![Direct3D12実行結果・GPU読戻し](Images/06_Gltf.png)

初期の描画領域は全サンプル共通で**1280×720ピクセル**です。掲載画像は変更前の800×600で取得したものを使用しています。

## 導入と実行

Visual Studio 2026、v145、Windows SDK 10.0.26100.0、C++20、x64です。Agility SDK 1.620.1／DXC 1.9.2609.5をNuGetで復元します。JSONヘッダーはMITライセンスのnlohmann/json v3.12.0です。

```powershell
git submodule update --init --recursive
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\06_Gltf\Direct3D12_06_Gltf.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_06_Gltf\Direct3D12_06_Gltf.exe
```

ソリューションではDirect3D12フォルダの`Direct3D12_06_Gltf`をスタートアッププロジェクトにします。単体ビルドにVulkan SDKは不要です。既定データは実行ファイル配下の`Assets/Models/ShaderBall/ShaderBall_Albedo.gltf`です。BINとAlbedo PNG、ライセンス表示もビルドで配置します。

## glTF読込と処理順

実装は[Sample.cpp](../../Samples/Direct3D12/06_Gltf/Sample.cpp)、CPUローダーは[Common/Gltf.cpp](../../Common/Gltf.cpp)です。[共通のglTF解説](../GltfLoading.md)にJSON、Accessor、右手系から左手系への変換、UVを反転しない理由、対応範囲をまとめています。

1. `OnInit`でglTFを読み、APIのデバイス・Queue・Swapchainを初期化します。材質のSampler設定もRoot Signature作成前に取得します。
2. `CreatePipeline`でb0のRoot CBV、t0のSRV Descriptor Table、s0のStatic Samplerを作ります。b0は頂点／ピクセルの両シェーダから参照するのでShaderVisibilityはALLです。
3. `CreateScene`で位置・UV・法線を保持する32-byte ModelVertexと32-bit Indexを転送します。Input LayoutはPOSITION（offset 0）とTEXCOORD（offset 12）です。法線は今回の照明なし描画では使いません。
4. AlbedoをWICでRGBA8へ展開し、Upload BufferからDefault Heapのテクスチャへコピーします。`COPY_DEST → PIXEL_SHADER_RESOURCE`へ遷移し、sRGB SRVを作ります。glTFのwrap・filterをStatic Samplerへ対応させ、mip 0だけなのでMaxLOD=0です。
5. `OnUpdate`で80-byteのMVP＋Base Color Factorを更新します。D3D12の定数バッファ格納領域は256-byte確保しています。
6. `OnRender`でRoot CBV・SRVを設定し、`DrawIndexedInstanced`、Present、Fence待機を実行します。

GPUリソースと同期はこのプロジェクトの中に明示しています。共通部品はCPUでのファイル・モデル読込と行列計算です。

## Depth Bufferで隠れた面を除く

Albedoだけの表示でもDepth Bufferは必要です。球と台座を同じメッシュへ結合しており、三角形は見える面の順に並んでいません。後から描いた遠い面が手前の面を上書きしないよう、ピクセルごとの奥行きを比較します。

`CreateDepth`はウィンドウと同じサイズの`DXGI_FORMAT_D32_FLOAT`を作り、`ALLOW_DEPTH_STENCIL`、初期状態`DEPTH_WRITE`で確保します。DSV HeapへViewを作り、`OMSetRenderTargets`でカラーRTVと一緒に設定します。

毎フレーム`ClearDepthStencilView`で**1.0**へクリアします。投影後の深度は0〜1、比較は`LESS`、DepthWriteMaskは`ALL`です。クリア直後は通常の面が通り、面を描くとその深度を保存します。以後、そのピクセルに届いた遠い面は比較に失敗してカラーを更新しません。透明な重ね合わせを扱う回ではありません。

深度は頂点のワールドZをそのまま書く値ではなく、投影と透視除算を経た値です。カメラは左手系、Projectionも0〜1を出力する`XMMatrixPerspectiveFovLH`です。Near/Farはモデルの大きさに合わせて設定します。

リサイズ時はGPUの完了を待って古い深度画像を解放し、新しい幅・高さで作り直して、既存DSV HeapのViewを更新します。最小化中は描画を停止します。球の手前側が台座や球の奥側を隠すこと、リサイズ後も前後関係が保たれることを確認します。Normal／AO画像はこの深度判定に使いません。

## 共通HLSLと確認

[Vertex.hlsl](../../Shaders/06_Gltf/Vertex.hlsl)と[Pixel.hlsl](../../Shaders/06_Gltf/Pixel.hlsl)をSM6.6／HLSL 2021でDXILへ事前コンパイルします。`Scene.hlsli`変更も再コンパイルの入力です。b0=MVP＋factor、t0=Albedo、s0=SamplerはVulkanでも同じソースです。

```powershell
& .\bin\x64\Debug\Direct3D12_06_Gltf\Direct3D12_06_Gltf.exe --model "D:\Assets\ShaderBall_Albedo.gltf"
& .\bin\x64\Debug\Direct3D12_06_Gltf\Direct3D12_06_Gltf.exe --frames 48 --test-window --log ".\.cache\gltf-d3d12.log" --screenshot ".\.cache\gltf-d3d12.png"
```

`--texture`はglTFで指定したAlbedoを別画像へ置き換える確認用オプションです。明示したコマンドラインの相対パスは起動時の作業ディレクトリ基準、glTF内の相対URIはglTFのある場所が基準です。glTF内の参照画像が存在することは置換時にもローダーで確認します。

DebugではD3D12 Debug Layerを有効化します。Shader Ballの矢印、上下の帯、UV境界、台座との奥行き、リサイズ・最小化からの復帰を確認します。[検証記録](../Verification.md#サンプル06-gltf-albedoの追加検証)に実機とビルド結果を記載しています。

前のテーマ：[05 OBJモデル](05_Model.md)。同じテーマの別API：[Vulkan 06](../Vulkan/06_Gltf.md)。

次のテーマ：[07 Lambert／Blinn-Phong](07_Lighting.md) → [08 PBR](08_PBR.md)。
