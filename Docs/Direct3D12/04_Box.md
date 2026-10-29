# Direct3D12 04: 頂点カラーBOXと3D座標変換

8頂点と36インデックスでBOXを描きます。固定カメラを右上手前に置き、定数バッファのModel・View・Projection行列と深度バッファで3面が見える立体を表示します。

![実行結果](Images/04_Box.png)

初期の描画領域は全サンプル共通で**1280×720ピクセル**です。掲載画像は変更前の800×600で取得したものを使用しています。

## ビルドと実行

Windows、Visual Studio 2026（v145）、Windows SDK 10.0.26100.0、x64が対象です。Agility SDK 1.620.1とDXC 1.9.2609.5をNuGet復元します。FL12_0／Shader Model 6.6対応GPUとドライバが必要です。Vulkan SDKはこのプロジェクト単体には不要です。

```powershell
msbuild .\Samples\Direct3D12\04_Box\Direct3D12_04_Box.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_04_Box\Direct3D12_04_Box.exe
```

コマンドはリポジトリ直下で実行します。D3D12のNuGet復元手順は[README](../../README.md)を参照してください。ESCまたは閉じるボタンで終了します。

## データと処理順

座標変換は左手系・行ベクトルです。`Common/Mesh.cpp`の`MakeSceneConstants`が`Model * View * Projection`を計算し、転置せずに`SceneConstants::mvp`へ保存します。HLSLは`row_major float4x4`と`mul(float4(position,1), mvp)`を使います。深度は0〜1、Clear値は1、比較はLESSです。リサイズ時にアスペクト比を更新します。

実装は[Sample.cpp](../../Samples/Direct3D12/04_Box/Sample.cpp)、宣言は[Sample.h](../../Samples/Direct3D12/04_Box/Sample.h)です。

- **`CreateScene`**: 頂点と32bitインデックスをUpload Heapに置きます。64バイトのMVP行列に対して定数バッファの領域は256バイトを確保し、Mapを維持します。
- **`CreatePipeline / CreateDepth`**: b0をVertex可視のRoot CBVとして配置します。D32_FLOATの深度リソースとDSV Heapを作り、PSOで深度テスト・書込みを有効化します。
- **`OnUpdate / OnRender`**: GPUが前フレームを完了した後にMVPを書きます。SetGraphicsRootConstantBufferView、IASetIndexBuffer、DrawIndexedInstancedで描き、各フレームで深度もクリアします。


## 共通HLSLとの対応

[頂点シェーダ](../../Shaders/04_Box/Vertex.hlsl)と[ピクセルシェーダ](../../Shaders/02_Triangle/Pixel.hlsl)は両APIで同じファイルです。Shader Model 6.6／HLSL 2021を指定し、D3D12ではDXIL（`.cso`）へコンパイルします。実行時コンパイルは行いません。ソースまたは共通includeの変更時にはビルドで再生成します。

MVP行列は`b0`で宣言します。Vulkanのbinding/locationは`Common.hlsli`のマクロで指定し、D3D12向けでは属性部分だけを除きます。詳細は[共通シェーダの規約](../SharedShaders.md)を参照してください。

## リサイズと確認

最小化中は描画を止め、復帰後にサイズ変更を反映します。リサイズ時はGPU完了を待ち、Swapchainの画像・Viewを作り直します。3Dサンプルでは深度画像も作り直します。初回は理解しやすさを優先し、複数フレームを並行実行しません。

```powershell
& .\bin\x64\Debug\Direct3D12_04_Box\Direct3D12_04_Box.exe --frames 48 --test-window --screenshot ".\.cache\Direct3D12_04_Box.png" --log ".\.cache\Direct3D12_04_Box.log"
```

`--test-window`は640×480への変更、最小化、1280×720への復帰を実行します。`--screenshot`はGPUから描画結果を読み戻してPNGを保存します。Debugの検証エラーが出た場合はログと終了コードで確認できます。画像・シェーダの欠落時も原因のファイル名を表示します。

テクスチャはsRGBとしてサンプリングし、色の補間・シェーダ計算は線形空間で行い、sRGB描画先で出力変換します。テクスチャの赤／緑／青／黄、上向き矢印、BOXの奥行きがAPI間で一致することを確認します。

初回検証の環境と結果は[検証記録](../Verification.md)に記載しています。

前のテーマ: [03 NDC座標のテクスチャ表示](03_Texture.md)

次のテーマ: [05 OBJモデルとテクスチャの読込](05_Model.md)

同じテーマの別API: [Vulkan](../Vulkan/04_Box.md)
