# Vulkan 05: OBJモデルとテクスチャの読込

外部OBJをCPU側の自作ローダーで読み込み、頂点・インデックスをGPUへ渡します。指定したPNGを1枚貼り、BOXサンプルと同じカメラ・深度処理で描画します。

![実行結果](Images/05_Model.png)

初期の描画領域は全サンプル共通で**1280×720ピクセル**です。掲載画像は変更前の800×600で取得したものを使用しています。

## ビルドと実行

Windows、Visual Studio 2026（v145）、Windows SDK 10.0.26100.0、x64が対象です。Vulkan SDKと付属DXCをインストールし、`VULKAN_SDK`を参照できる状態でVSを起動します。実行GPUにはVulkan 1.3、Dynamic Rendering、Synchronization2、graphics/present両対応Queueが必要です。

```powershell
msbuild .\Samples\Vulkan\05_Model\Vulkan_05_Model.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Vulkan_05_Model\Vulkan_05_Model.exe
```

コマンドはリポジトリ直下で実行します。D3D12のNuGet復元手順は[README](../../README.md)を参照してください。ESCまたは閉じるボタンで終了します。

## データと処理順

`Common/Mesh.cpp`は`v`・`vt`・`vn`・`f`を読みます。位置・UV・法線インデックスの組をキーにするため、UV境界や面法線が違う頂点を正しく分割できます。正インデックスは1始まり、負インデックスはその行までに読み込んだ配列末尾を基準に解決します。UVのVだけを`1-v`に変換し、頂点座標や法線はそのまま保持します。

実装は[Sample.cpp](../../Samples/Vulkan/05_Model/Sample.cpp)、宣言は[Sample.h](../../Samples/Vulkan/05_Model/Sample.h)です。

- **`CreateScene`**: 共通OBJローダーの結果から頂点・32bitインデックスを作り、WICで読んだ画像をImageへ転送します。法線はCPUデータに保持しますが、初回のシェーダでは照明計算に使いません。
- **`CreatePipeline`**: set 0のbinding 0にMVP Uniform Buffer、1に画像、2にSamplerを配置します。位置とUVのlocationをD3D12と共有するHLSLで明示します。
- **`OnRender`**: BOXサンプルのUniform・深度処理と、テクスチャサンプルのDescriptorを組み合わせます。頂点座標の上下補正をHLSLへ追加せず、ViewportでAPI差を吸収します。


## 共通HLSLとの対応

[頂点シェーダ](../../Shaders/05_Model/Vertex.hlsl)と[ピクセルシェーダ](../../Shaders/03_Texture/Pixel.hlsl)は両APIで同じファイルです。Shader Model 6.6／HLSL 2021を指定し、Vulkanでは`-spirv -fspv-target-env=vulkan1.3`でSPIR-V（`.spv`）へコンパイルします。実行時コンパイルは行いません。ソースまたは共通includeの変更時にはビルドで再生成します。

MVP行列は`b0`で宣言します。画像は`Texture2D : register(t0)`、Samplerは`SamplerState : register(s0)`で宣言します。Vulkanのbinding/locationは`Common.hlsli`のマクロで指定し、D3D12向けでは属性部分だけを除きます。詳細は[共通シェーダの規約](../SharedShaders.md)を参照してください。

## OBJと画像の差し替え

```powershell
& .\bin\x64\Debug\Vulkan_05_Model\Vulkan_05_Model.exe --model "D:\Assets\model.obj" --texture "D:\Assets\albedo.png"
```

指定した相対パスは起動時の作業ディレクトリから解決します。省略時は実行ファイル配下の`Assets/Models/Cube.obj`と`Assets/Textures/UV.png`を使います。

BlenderではWavefront OBJで位置・UV・法線を書き出してください。凹多角形は書き出し前に三角形化し、位置・向き・スケールを確認します。サンプルのカメラは原点を見ており、一辺2程度のモデルが見やすい設定です。モデルの座標系・スケール・Up軸をローダーが自動変更することはありません。

対応する面は`v/vt`または`v/vt/vn`で、三角形・凸多角形を扱います。UVのない面、不正数値、0や範囲外のインデックスにはファイル名と行番号付きエラーを出します。`mtllib`と`usemtl`は読み飛ばし、MTL・複数マテリアル・照明は扱いません。画像はコマンドラインで指定した1枚を使用します。

## リサイズと確認

最小化中は描画を止め、復帰後にサイズ変更を反映します。リサイズ時はGPU完了を待ち、Swapchainの画像・Viewを作り直します。3Dサンプルでは深度画像も作り直します。初回は理解しやすさを優先し、複数フレームを並行実行しません。

```powershell
& .\bin\x64\Debug\Vulkan_05_Model\Vulkan_05_Model.exe --frames 48 --test-window --screenshot ".\.cache\Vulkan_05_Model.png" --log ".\.cache\Vulkan_05_Model.log"
```

`--test-window`は640×480への変更、最小化、1280×720への復帰を実行します。`--screenshot`はGPUから描画結果を読み戻してPNGを保存します。Debugの検証エラーが出た場合はログと終了コードで確認できます。画像・シェーダの欠落時も原因のファイル名を表示します。

テクスチャはsRGBとしてサンプリングし、色の補間・シェーダ計算は線形空間で行い、sRGB描画先で出力変換します。テクスチャの赤／緑／青／黄、上向き矢印、BOXの奥行きがAPI間で一致することを確認します。

初回検証の環境と結果は[検証記録](../Verification.md)に記載しています。

前のテーマ: [04 頂点カラーBOXと3D座標変換](04_Box.md)

同じテーマの別API: [Direct3D12](../Direct3D12/05_Model.md)

次のテーマ: [06 glTFモデルのAlbedo表示](06_Gltf.md)
