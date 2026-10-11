# Direct3D12 01: APIの初期化と画面クリア

ウィンドウを作り、GPU、Swapchain、コマンド、同期オブジェクトを準備して背景色を表示します。描画パイプラインやシェーダはまだ作りません。

![実行結果](Images/01_Initialization.png)

## ビルドと実行

Windows、Visual Studio 2026（v145）、Windows SDK 10.0.26100.0、x64が対象です。Agility SDK 1.619.6とDXC 1.9.2609.5をNuGet復元します。FL12_0／Shader Model 6.6対応GPUとドライバが必要です。Vulkan SDKはこのプロジェクト単体には不要です。

```powershell
msbuild .\Samples\Direct3D12\01_Initialization\Direct3D12_01_Initialization.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_01_Initialization\Direct3D12_01_Initialization.exe
```

コマンドはリポジトリ直下で実行します。D3D12のNuGet復元手順は[README](../../README.md)を参照してください。ESCまたは閉じるボタンで終了します。

## データと処理順

画面サイズは初期値800×600です。`OnInit`で初期化、`OnRender`でクリアとPresent、`OnResize`でサイズ依存のリソース再作成、`OnDestroy`でGPU完了を待って解放します。Win32のメッセージ処理だけを`Common/Application.cpp`へ分離しています。

実装は[Sample.cpp](../../Samples/Direct3D12/01_Initialization/Sample.cpp)、宣言は[Sample.h](../../Samples/Direct3D12/01_Initialization/Sample.h)です。

- **`OnInit`**: DebugではD3D12 Debug Layerを有効化します。DXGIの高性能ハードウェアアダプタからFL12_0のデバイスを選び、Shader Model 6.6対応を確認します。Command Queueと2枚のFlip Discard Swapchainを作成します。
- **`CreateTargets`**: Swapchain本体はRGBA8 UNORMで作り、RTVをRGBA8 UNORM sRGBにします。RTV Heapから各バックバッファのハンドルを割り当てます。
- **`OnRender / WaitForGpu`**: PRESENT→RENDER_TARGETのバリア、Clear、RENDER_TARGET→PRESENT、ExecuteCommandLists、Presentの順です。Fenceとイベントで1フレームごとにCPUを待機させます。


## 共通HLSLとの対応

このサンプルはシェーダを使用しません。次の三角形から、MSBuildがHLSLを事前コンパイルし、アプリがバイナリを読む構成になります。

## リサイズと確認

最小化中は描画を止め、復帰後にサイズ変更を反映します。リサイズ時はGPU完了を待ち、Swapchainの画像・Viewを作り直します。3Dサンプルでは深度画像も作り直します。初回は理解しやすさを優先し、複数フレームを並行実行しません。

```powershell
& .\bin\x64\Debug\Direct3D12_01_Initialization\Direct3D12_01_Initialization.exe --frames 48 --test-window --screenshot ".\.cache\Direct3D12_01_Initialization.png" --log ".\.cache\Direct3D12_01_Initialization.log"
```

`--test-window`は640×480への変更、最小化、800×600への復帰を実行します。`--screenshot`はGPUから描画結果を読み戻してPNGを保存します。Debugの検証エラーが出た場合はログと終了コードで確認できます。画像・シェーダの欠落時も原因のファイル名を表示します。

テクスチャはsRGBとしてサンプリングし、色の補間・シェーダ計算は線形空間で行い、sRGB描画先で出力変換します。テクスチャの赤／緑／青／黄、上向き矢印、BOXの奥行きがAPI間で一致することを確認します。

初回検証の環境と結果は[検証記録](../Verification.md)に記載しています。

次のテーマ: [02 NDC座標の三角形と頂点カラー](02_Triangle.md)

同じテーマの別API: [Vulkan](../Vulkan/01_Initialization.md)
