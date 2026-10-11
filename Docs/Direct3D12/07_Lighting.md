# Direct3D12 07: LambertとBlinn-Phongのライティング

06のglTF Albedo表示へ法線・方向光・視線を加えます。Lambertの拡散反射と、Blinn-Phongの鏡面反射を切り替え、球の明暗とハイライトを比較します。

![Direct3D12の実GPU描画](Images/07_Lighting.png)

## ビルドと実行

Visual Studio 2026／v145、Windows SDK 10.0.26100.0、C++20、x64です。Agility SDK 1.619.6／DXC 1.9.2609.5をNuGetで復元します。 nlohmann/jsonは06と同じ固定Submoduleを使用します。

```powershell
git submodule update --init --recursive
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\07_Lighting\Direct3D12_07_Lighting.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_07_Lighting\Direct3D12_07_Lighting.exe
```

VSでは`Direct3D12_07_Lighting`をスタートアッププロジェクトにします。Vulkan SDKなしで単体ビルドできます。アセットとシェーダは実行ファイル基準で読みます。

## 操作と比較

| キー | CLI | 表示 |
|---|---|---|
| 1 | `--shading lambert` | Lambert拡散反射 |
| 2 | `--shading blinn-phong` | Lambert＋Blinn-Phong。既定値 |
| 3 | `--shading albedo` | 06と同じAlbedoのみ |
| Space | `--rotate-light`で回転開始 | 方向光の回転を開始／停止 |

![Lambertの実GPU描画](Images/07_Lighting-lambert.png)

Lambertは法線と光の角度による明暗、Blinn-Phongはそれに視線依存の白いハイライトを加えます。光の回転を止めた同じ条件で見比べます。

## データからシェーダまで

[Sample.cpp](../../Samples/Direct3D12/07_Lighting/Sample.cpp)にAPIリソースと描画処理、[共有Pixel Shader](../../Shaders/07_Lighting/Pixel.hlsl)に反射式を記述しています。[共通解説](../LightingAndPBR.md)ではN・L・V・H、逆転置行列、線形色、式を段階的に説明しています。

1. `LoadGltfAlbedo`で06と同じ位置・UV・法線・Albedo・Base Color Factor・Samplerを読みます。既定モデルはShaderBall_Albedo.gltfです。
2. POSITION（offset 0）、TEXCOORD（12）、NORMAL（20）のInput Layoutを定義します。32-byteのModelVertexをVertex Buffer、32-bitのIndexをIndex Bufferへ転送します。b0はRoot CBVで、Vertex／Pixelの両方から使うためShaderVisibility=ALLです。t0は1個のSRV Descriptor Table、s0はglTFのfilter／wrapに従うStatic Samplerです。
3. 320-byteのLightingConstantsを、256-byte境界へ切り上げた512-byteのUpload Heapへ置きます。MapしたCPUアドレスを毎フレーム更新し、GPUアドレスをSetGraphicsRootConstantBufferViewで渡します。
4. 頂点シェーダでWorld位置・World法線を作り、ViewProjectionで画面へ投影します。
5. Pixel Shaderで法線を再正規化し、`albedo/π * saturate(N・L)`と、選択時に`0.3 * saturate(N・H)^32`を計算します。
6. 一定の小さな環境項を加え、ReinhardとsRGB出力で画面へ表示します。Albedoモードは反射計算・Reinhardを通しません。

各フレームのGPU完了を待つ同期方式を維持しています。光・表示の変更はCPU定数の更新で行い、シェーダを実行時コンパイルしません。

## Depth Bufferとライティング

`CreateDepth`でウィンドウサイズの`DXGI_FORMAT_D32_FLOAT`を作ります。`ALLOW_DEPTH_STENCIL`、初期状態`DEPTH_WRITE`、最適化クリア値1.0を指定し、DSV HeapにViewを作ります。`OnRender`でRTVとDSVを`OMSetRenderTargets`へ渡し、毎フレーム`ClearDepthStencilView`で1.0へクリアします。

PSOのDepthEnableはTRUE、DepthWriteMaskはALL、DepthFuncはLESSです。遠い面を先に描いても、より小さい深度を持つ手前の面が通り、その深度を保存します。近い面を先に描いた場合には、後の遠い面が比較に失敗します。リサイズ時はFenceでGPU完了を待ち、古い深度リソースを解放して新しい大きさで作り直します。

カメラと投影は左手系、深度範囲は0〜1です。球と台座の前後関係はこの比較で決まり、Pixel Shaderの明るさや反射モデルは比較値を変えません。Normal Mapを使う08も、変わるのは反射計算用の法線で、輪郭や深度を動かしません。Shadow Mapが光から見た遮蔽を判定する処理は別のテーマです。

## 検証と次の回

```powershell
& .\bin\x64\Debug\Direct3D12_07_Lighting\Direct3D12_07_Lighting.exe --shading lambert --frames 48 --test-window --screenshot ".\.cache\Direct3D12-07.png" --log ".\.cache\Direct3D12-07.log"
.\Tools\TestLoaders.ps1
.\Tools\TestSamples.ps1 -Topics 07_Lighting
.\Tools\TestLighting.ps1
```

`--model`で06と同じプロフィールのglTFへ差し替えられます。`--texture`はBase Colorだけを置換します。指定した相対パスは起動時の作業ディレクトリ、glTF内のURIはglTFの場所が基準です。

DebugではD3D12 Debug Layerを有効にして検証しています。CPUの材質試験、実GPUの既知画素試験、リサイズ、最小化復帰、終了を確認し、[検証記録](../Verification.md)にバージョンを残しています。添付画像はこのAPIによる実GPU描画です。

07のBlinn-Phongは鏡面色と強さを独立に指定する経験的なモデルです。次の08では同じ形状・UV・カメラを使い、金属と粗さを入力へ加えてPBRへ進みます。

前：[06_Gltf](06_Gltf.md)。 次：[08 PBR](08_PBR.md)。 別API：[Vulkan 07](../Vulkan/07_Lighting.md)。
