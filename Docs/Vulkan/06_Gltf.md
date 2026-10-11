# Vulkan 06: glTFモデルのAlbedo表示

Shader BallのglTFをCPUで読み込み、VulkanのVertex／Index Buffer、Uniform Buffer、AlbedoのImage Viewで描画します。照明を計算せずBase Colorだけを表示します。

![Vulkan実行結果・GPU読戻し](Images/06_Gltf.png)

## 導入と実行

Visual Studio 2026、v145、Windows SDK 10.0.26100.0、C++20、x64です。`VULKAN_SDK`のヘッダー・ライブラリ・DXCを使い、Vulkan 1.3のDynamic RenderingとSynchronization2を使用します。JSONはMITライセンスのnlohmann/json v3.12.0です。

```powershell
git submodule update --init --recursive
msbuild Samples\Vulkan\06_Gltf\Vulkan_06_Gltf.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Vulkan_06_Gltf\Vulkan_06_Gltf.exe
```

ソリューションのVulkanフォルダにある`Vulkan_06_Gltf`をスタートアッププロジェクトにします。SDK導入後はVisual Studioを再起動して`VULKAN_SDK`を反映してください。既定データは実行ファイル配下の`Assets/Models/ShaderBall/ShaderBall_Albedo.gltf`です。BIN・Albedo PNG・ライセンス表示もビルドで配置します。

## glTF読込と処理順

実装は[Sample.cpp](../../Samples/Vulkan/06_Gltf/Sample.cpp)、CPUローダーは[Common/Gltf.cpp](../../Common/Gltf.cpp)です。[共通のglTF解説](../GltfLoading.md)にAccessor、座標・UV・材質の対応範囲をまとめています。

1. `OnInit`でglTFを読み、Instance・Surface・Physical Device・Device・Queue・Swapchainを作ります。
2. `CreatePipeline`でset 0のbinding 0=Uniform Buffer、1=Sampled Image、2=Samplerを定義します。MVPとBase Color Factorを同じUniformに入れるためbinding 0はVERTEX／FRAGMENTの両ステージから参照します。
3. `CreateScene`で32-byte ModelVertexと32-bit IndexをBufferへ書きます。位置はlocation 0、UVはlocation 1で、法線はCPU／頂点データに保持しますが今回のシェーダでは使いません。
4. 80-byteのMVP＋factorをUniformへ書き、格納サイズを`minUniformBufferOffsetAlignment`へ揃えます。Descriptorのrangeは実データの80バイトです。非coherentメモリではFlushします。
5. PNGをWICでRGBA8へ展開し、Staging BufferからsRGB Imageへコピーします。`UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL`の遷移をSynchronization2で記録します。
6. glTFのwrap・filterをSamplerへ対応させます。mip 0だけのためMinLOD=MaxLOD=0です。Descriptor SetへUniform・Image View・Samplerを書き、描画時にbindします。
7. `OnRender`でDynamic Rendering、`vkCmdDrawIndexed`、Submit2、Present、Fence待機を実行します。

GPUのAPI処理はサンプル内にあります。CPUファイル読込と行列計算を共有し、描画APIは抽象化していません。

## Depth Bufferで隠れた面を除く

球と台座の三角形は見える順に並んでいません。Albedoだけの描画でも、遠い三角形が手前を上書きしないよう、ピクセルごとのDepthを比較します。

`CreateDepth`はSwapchainと同じ幅・高さで`VK_FORMAT_D32_SFLOAT`のImageとImage Viewを作ります。usageは`DEPTH_STENCIL_ATTACHMENT`、Viewとバリアのaspectは`DEPTH`です。作成時にこの深度形式が対応していることを確認します。

`VkPipelineDepthStencilStateCreateInfo`でDepth TestとDepth Writeを有効にし、比較を`VK_COMPARE_OP_LESS`にします。Dynamic Renderingの`pDepthAttachment`へ深度Viewを設定し、loadOp=CLEAR、clear depth=**1.0**とします。投影後の深度0〜1で、より小さい値を持つ手前の面だけが比較を通り、カラーとDepthを更新します。

Imageのレイアウトは最初の描画前に`UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL`へ遷移します。二回目以降も深度書込を次の深度テスト／書込へ同期し、Early／Late Fragment TestsとDepth Attachmentのアクセスをバリアへ含めます。Fenceの完了待機に加えて、Imageのレイアウトとアクセスも明示する点が大切です。

リサイズ時はDeviceの処理完了を待ち、Swapchainと深度Image／Viewを作り直します。初回使用の状態もリセットし、新しい深度画像の遷移を記録します。最小化中は描画を止めます。球の手前側と台座の前後関係が、ウィンドウサイズを変えても保たれることを確認します。

深度0〜1はDirect3D12と共通です。Vulkanの負のviewport heightは画面Yの向きを揃える処理で、Depthの0〜1やglTFのUVを反転する処理ではありません。Near/Farはモデルの大きさに合わせた左手系Perspective行列で設定します。

## 共通HLSLと確認

[Vertex.hlsl](../../Shaders/06_Gltf/Vertex.hlsl)と[Pixel.hlsl](../../Shaders/06_Gltf/Pixel.hlsl)をSM6.6／HLSL 2021、`-spirv -fspv-target-env=vulkan1.3`で事前コンパイルします。ビルドで`spirv-val`も実行します。`Scene.hlsli`の変更も再コンパイルの入力に含まれます。

```powershell
& .\bin\x64\Debug\Vulkan_06_Gltf\Vulkan_06_Gltf.exe --model "D:\Assets\ShaderBall_Albedo.gltf"
& .\bin\x64\Debug\Vulkan_06_Gltf\Vulkan_06_Gltf.exe --frames 48 --test-window --log ".\.cache\gltf-vulkan.log" --screenshot ".\.cache\gltf-vulkan.png"
```

`--texture`で参照Albedoを別画像へ置換できます。コマンドラインの相対パスは起動時の作業ディレクトリ、glTF内の相対URIはglTFのある場所が基準です。glTF内の参照画像が存在することは置換時にもローダーで確認します。

DebugではValidation Layerを有効化します。`Tools/TestSamples.ps1`は同期検証も有効にして、画像、リサイズ、最小化／復帰、終了を確認します。AlbedoはsRGBから線形へ変換してfactorを掛け、sRGBのカラーAttachmentへ出力します。[検証記録](../Verification.md#サンプル06-gltf-albedoの追加検証)に実機と結果を記載しています。

前のテーマ：[05 OBJモデル](05_Model.md)。同じテーマの別API：[Direct3D12 06](../Direct3D12/06_Gltf.md)。

次のテーマ：[07 Lambert／Blinn-Phong](07_Lighting.md) → [08 PBR](08_PBR.md)。
