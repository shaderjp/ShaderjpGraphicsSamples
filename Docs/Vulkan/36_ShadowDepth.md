# Vulkan 第36 Shadow Mapの投影深度と線形深度を比較する

[共通の入門記事](../ShadowDepth.md)で観察の順序と数式を説明しています。このページでは、実行方法とVulkanの処理を追います。

![Vulkanの実GPU描画](Images/36_ShadowDepth-directional.png)

## ビルドと起動

Visual Studio 2026／v145、Windows SDK 10.0.26100.0、C++20、x64 Debug／Releaseを使います。初期クライアント領域は1920×1080です。Dear ImGuiは既存のv1.92.9bを使い、日本語フォントはBIZ UDゴシックv1.051を固定Submoduleから配置します。追加のモデル作成ツールは不要です。

~~~powershell
git submodule update --init --recursive
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Vulkan\36_ShadowDepth\Vulkan_36_ShadowDepth.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Vulkan_36_ShadowDepth\Vulkan_36_ShadowDepth.exe
~~~

インストール済みVulkan SDKと付属DXCを使い、Vulkan 1.3を対象にします。Dynamic Rendering／Synchronization2を使用し、ビルド時にSPIR-Vも検証します。

## 操作とプリセット

「方向光：平行投影」「スポットライト：透視投影」「バイアスなし」「解像度256」を切り替えます。上段の左は投影深度で影を比較、右は明示的に保存した線形深度で比較します。保存方式の選択で両側を同じ方式にもできます。

中段はライトの投影深度と線形深度です。下段左は投影深度から復元したライト視点の深度、右はカメラから見たShadow Visibilityです。上段をクリックすると受光点に対応するライト画像上の位置を強調します。数値欄は中心テクセルの比較を示します。必要なら左の欄をスクロールしてください。

PCF、受光点のバイアス、Shadow Map解像度、ライトのNear／Farを変更できます。カメラは固定したD32のReverse-Zです。Resetは起動時の設定へ戻します。

| CLI | 値 |
|---|---|
| --depth-preset | directional / spot / bias / resolution |
| --depth-light | directional / spot |
| --depth-encoding | projected / linear / compare |
| --depth-bias | ライトの視線方向のm単位、0〜1 |
| --depth-resolution | 256 / 1024 / 2048 |
| --depth-pcf | off / on |
| --depth-near / --depth-far | ライトのNear／Far、m単位 |
| --depth-display-far | 線形画像と図の表示上限 |
| --depth-graph | mapping / spacing |

ライトの投影方式は--depth-lightで選びます。D16／D24、無限遠、カメラの平行投影は第36回では使用しません。

~~~powershell
& .\bin\x64\Debug\Vulkan_36_ShadowDepth\Vulkan_36_ShadowDepth.exe --depth-preset spot --frames 3 --capture-gpu --screenshot ".\.cache\36_ShadowDepth.png" --log ".\.cache\36_ShadowDepth.log"
~~~

プリセットを先に適用し、個別CLI指定をその後に適用します。--width／--height、--no-ui、--frames、--screenshot、--logも共通の指定として使えます。フォントなどは実行ファイル基準で読み込みます。

## リソースと描画の順序

[Sample.cpp](../../Samples/Vulkan/36_ShadowDepth/Sample.cpp)に初期化、画像作成、パイプライン、描画、同期、読戻し、終了処理を置いています。CPUの設定・幾何データ・投影計算と日本語UIだけをSharedへ置き、共有レンダリングAPIは作っていません。

D16_UNORM／D24_UNORM_S8_UINT／D32_SFLOATの深度アタッチメントとサンプリング対応を問い合わせます。D24のStencilは使いません。必要な用途に非対応の形式は選択不可とし、別形式へ自動で置き換えません。

ライト用にD32とR32_SFLOATを作ります。比較用カメラは左右別々の深度、RGBA8の色、RGBA32_FLOATの測定データを持ちます。測定データはview z、面のID、Shadow Visibility、有効画素です。

Dynamic Renderingでライト、左右のシーン、表示、ImGuiの順に描画します。画像は深度／カラーアタッチメントとShader Readのレイアウトを明示的に遷移させます。D24では深度とStencilの両Aspectを遷移させ、サンプリングするViewは深度だけです。

set 0のbinding 0がb0、binding 1がt0、binding 3〜9がt1〜t7です。深度の参照はLoadなのでSamplerは不要です。UIのs0は既存どおりbinding 2です。Uniform BufferのオフセットはデバイスのminUniformBufferOffsetAlignmentへ整列させます。

シーン、表示、読戻しのViewportは負の高さを使い、共有HLSLのY方向と一致させます。深度範囲は0〜1のままです。Fenceの完了を待ってからCPUで見えるバッファを書き換えます。

## HLSLで追うポイント

- [DepthVertex.hlsl](../../Shaders/Depth/DepthVertex.hlsl)：View変換とProjectionを分け、投影から通常Z／Reverse-Zを作ります。
- [DepthPixel.hlsl](../../Shaders/Depth/DepthPixel.hlsl)：色と測定データを出力します。第36回は受光点をライト空間へ移し、同じ定義の深度同士で比較します。
- [ShadowLinear.hlsl](../../Shaders/Depth/ShadowLinear.hlsl)：ライト視点のzをR32_FLOATへ保存します。SV_Depthへの書き込みではありません。
- [DepthDisplay.hlsl](../../Shaders/Depth/DepthDisplay.hlsl)：保存値、復元深度、Shadow Visibilityを表示し、数値検証用の出力も生成します。

SM6.6／HLSL 2021／row-majorで事前コンパイルします。表示を見やすくするための線形化と、実際に前後判定を行う深度バッファは別の処理です。

## 数値の読戻しと検証

--capture-gpuを指定すると、PNGと同じ場所に「画像名-gpu」フォルダを作ります。metadata.jsonに寸法・Near／Far・形式・投影方式などを記録し、standard.bin／reversed.binにRGBA32_FLOATの読戻しを保存します。第36回のファイル名は共通ですが、中身は左右の保存方式の比較です。

1画素はlittle-endian float32の4要素で、深度画像からLoadしたd、別出力のview z、面ID、有効画素です。D16／D24の元のパックされたメモリを直接保存したものではありません。第36回のshadow.binは投影深度、明示的線形深度、予約値、有効画素です。

~~~powershell
.\Tools\ValidateProjects.ps1
.\Tools\TestDepthCpu.ps1
.\Tools\TestDepth.ps1 -Api Vulkan
.\Tools\TestDepth.ps1 -Api Vulkan -Configuration Release
.\Tools\TestDepth.ps1 -Api Vulkan -CaptureDocs
.\Tools\TestSamples.ps1 -Apis Vulkan -Topics 36_ShadowDepth -CaptureDocs
~~~

PythonのGPU検証は既存と同じNumPyのある環境を使います。既定はBlender同梱Pythonで、-PythonPathで変更できます。Pillowは不要です。

起動、リサイズ、最小化／復帰、撮影、終了と、実ウィジェットの編集／Resetを検証します。初期サイズの指定はSampleDefaultsとMSBuildのSampleClientWidth／SampleClientHeightを揃えています。実測結果は[検証記録](../DepthVerification.md)を参照してください。

別API：[Direct3D12版](../Direct3D12/36_ShadowDepth.md)。共通記事：[入門解説](../ShadowDepth.md)。
