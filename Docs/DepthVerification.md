# 第35・36回 深度バッファの検証記録

検証日：2026-11-10。Direct3D12／Vulkanの第35回DepthPrecision、第36回ShadowDepth、計4アプリを対象にしました。[共通解説](DepthPrecision.md)、[Shadow Mapの解説](ShadowDepth.md)、[数値結果](DepthResults.json)、[環境](DepthEnvironment.json)、[撮影条件とSHA-256](DepthCaptures.json)を参照してください。

## 環境

| 項目 | 実測・使用版 |
|---|---|
| OS／CPU | Windows 11 Pro 10.0.26300、Intel Core i9-13900F |
| GPU／ドライバー | NVIDIA GeForce RTX 4070、621.01（Windows表示32.0.16.2101） |
| Visual Studio | Community 2026、18.10.12201.205 |
| C++ | v145、MSVC 19.51.36257、tool directory 14.51.36231、C++20、x64 |
| Windows SDK | 10.0.26100.0 |
| Direct3D12 | Agility SDK 1.620.1、SDK export 620 |
| D3D12 DXC | 1.9.2609.5、01b62ad47 |
| Vulkan | SDK 1.4.341.1、target Vulkan 1.3 |
| Vulkan DXC | SDK付属1.9.0.5180、e35541826（DLL表示1.10） |
| SPIR-V検査 | SPIRV-Tools v2026.1、fbe4f3ad |
| Shader | SM6.6、HLSL 2021、row-major |
| UI／日本語 | Dear ImGui 1.92.9b、BIZ UDゴシックv1.051 |

## 実行した検査

NuGet Restoreと全117プロジェクト（114 APIアプリ＋3 Support）のx64 Debug／Releaseビルドが成功しました。新規4アプリは最終変更後にも両構成でビルドしています。project／filtersをXMLとして検査し、ファイルの欠落・余剰・重複・未宣言Filterへの参照はありません。VulkanのShaderはビルド時にspirv-valを通しています。

CPU試験は端点、既知の数値、単調性、逆変換、有限／無限遠、平行投影、UNORMの反転対称性、floatの表現間隔、低ビット模型、不正入力を検査します。GPU試験は各API・各構成で22条件、計88条件です。この内訳は数値・描画13条件、実UI2条件、不正入力7条件です。これにDebugの掲載画像22条件と、4アプリ×2構成のライフサイクル試験8条件を加えました。

- 実GPUのD16／D24／D32をサンプリングして読み戻し、別のカラー出力に記録したview zと独立したfloat64の式から予測した保存値を照合。
- 有限遠／無限遠／平行投影と、通常Z／Reverse-Zの両方を検査。
- 方向光／スポットライトの保存深度を照合し、投影深度と明示的な線形深度による影を比較。
- PCFのON／OFFで影の境界の画素が変わること、解像度256の画像が実際に256×256で読み戻されることを検査。
- 実ImGuiウィジェットへの入力でNearまたはバイアスを変更し、GPUの表示画素が変化。Reset後の比較領域は基準画像と完全一致。画素選択の入力も検査。
- Near=0、NaN、Near=Far、平行投影＋無限遠、未知の形式、非整数の解像度、未実装の光源指定を明示的に拒否。
- 起動、640×480へのリサイズ、最小化／復帰、1920×1080への復帰、撮影、終了を検査。

正常Debug試験はグラフィックス検証レイヤーを有効にし、Vulkanでは同期検証も有効にしました。検証エラーは0件です。既存のCPUローダーと、第09回ImGuiの両API・両構成の回帰試験も成功しました。全既存アプリを今回GPUで再実行したという意味ではありません。

## 数値結果の読み方

数値試験のウィンドウは960×540、シーンの読み戻しは377×175です。UI試験と掲載画像は1920×1080です。次の値は両API・両構成で一致しました。

| 条件 | 通常Zの保存値の最大差 | Reverse-Zの保存値の最大差 |
|---|---:|---:|
| 遠方の近接面、D16 | 7.6235e-6 | 8.5579e-6 |
| 遠方の近接面、D24 | 8.7532e-8 | 2.9743e-8 |
| 遠方の近接面、D32 | 8.7532e-8 | 3.0372e-11 |

この差はm単位の距離誤差ではなく、読み戻した無次元の保存値dと、GPUが別出力したview zから計算したdとの差です。view z自体もfloat32であり、頂点変換・補間・保存の丸めを含むため、保存形式だけの量子化誤差とは区別します。許容差はD16が2/65535、D24が4/16777215、D32が4e-7です。

Near=0.1m、Far=10000m、約1km先の傾いた平行な2面、間隔1cmというD32のストレス条件では、中心画素の面IDは通常Zが奥の10、Reverse-Zが手前の11でした。Reverse-Zで手前のIDが記録された領域のうち、通常Zで異なるIDになった割合は76.66%でした。この割合や縞模様は固定の仕様ではなく、このGPUと条件での観測結果です。

方向光の投影深度と線形深度の最大差は1.1921e-7、スポットライトの投影式との最大差は1.4065e-7でした。PCFなしで保存方式だけを変えた画像の比較領域では、RGB差が2/255を超える画素は0でした。PCFを有効にすると、方向光628画素、スポットライト395画素がこの閾値を超えて変わりました。

低ビット模型と精度グラフはCPUの説明用計算です。GPU数値試験と混ぜて「GPUで測った精度」とは呼びません。UIの読戻しにはGPU完了待ちを使うため、この教材のフレーム時間は実用レンダラーの性能評価には使いません。

## 再現する

~~~powershell
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild ShaderjpGraphicsSamples.sln -m -p:Configuration=Debug -p:Platform=x64
msbuild ShaderjpGraphicsSamples.sln -m -p:Configuration=Release -p:Platform=x64
.\Tools\ValidateProjects.ps1
.\Tools\TestDepthCpu.ps1
.\Tools\TestDepth.ps1 -Configuration Debug -CaptureDocs
.\Tools\TestDepth.ps1 -Configuration Release
.\Tools\TestSamples.ps1 -Configuration Debug -Topics 35_DepthPrecision,36_ShadowDepth -CaptureDocs
.\Tools\TestSamples.ps1 -Configuration Release -Topics 35_DepthPrecision,36_ShadowDepth
.\Tools\TestLoaders.ps1
.\Tools\TestImGui.ps1 -Configuration Debug
.\Tools\TestImGui.ps1 -Configuration Release
~~~

GPU試験のログ・JSON・バイナリ読戻しは.cache/depth-gpu、ライフサイクル試験のログは.cache/validationへ保存されます。PythonはNumPyが必要で、TestDepth.ps1の-PythonPathで指定できます。記事用PNGは実GPUから撮影した1920×1080の26枚です。22枚のプリセット／グラフ画像と、4枚のライフサイクル試験画像からなります。

掲載画面は文字・配置・グラフ・描画内容を目視確認し、両APIの対応する13組も画素比較しました。API名を含む上部76行を除き、12組は完全一致、バイアスなしの画像だけ1画素に最大12/255のRGB差がありました。[比較記録](DepthCaptureComparison.json)に保存しています。自己遮蔽を意図的に発生させる条件も含むため、API間の完全一致を正しさの要件にはしていません。

未対応の深度形式は選択不可またはCLIでエラーにします。この環境では3形式すべてを実行できました。未対応GPU実機、他社GPU、点光源Cubemap、CSM、VSMは今回の検証対象に含みません。
