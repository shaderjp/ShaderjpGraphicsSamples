# 初回サンプルの検証記録

検証日: 2026-10-11（日本時間）。結果は以下の実機・SDK・コンパイラの組合せに対するものです。

## 環境

| 項目 | 検証した環境 |
|---|---|
| OS | Windows 11 Pro、10.0.26300 |
| Visual Studio | Community 2026、18.10.0（18.10.12201.205） |
| C++ | v145、C++20、x64 |
| Windows SDK | 10.0.26100.0 |
| GPU | NVIDIA GeForce RTX 4070 |
| GPUドライバ | NVIDIA 610.88、Windowsドライバ32.0.16.1088 |
| Agility SDK | Microsoft.Direct3D.D3D12 1.619.6、D3D12SDKVersion=619 |
| D3D12用DXC | Microsoft.Direct3D.DXC 1.9.2609.5、commit 01b62ad47 |
| Vulkan SDK | 1.4.341.1 |
| Vulkan用DXC | SDK付属、バージョン文字列1.9.0.5180、commit e35541826（ライブラリ表示は1.10） |
| シェーダ | SM6.6、HLSL 2021、Vulkanターゲット環境1.3 |

## ビルド・静的検証

- NuGetをMSBuildの`RestorePackagesConfig=true`で復元できることを確認しました。
- MSBuildで全10プロジェクトのDebug／Releaseをビルドしました。最終ビルドはコンパイラ警告・エラー0件です。
- Visual Studioの`devenv.com /Rebuild "Debug|x64"`でも全10プロジェクトを再構築し、成功10・失敗0を確認しました。
- 全10組の`.vcxproj`／`.filters`をXML照合し、項目の過不足・重複・未宣言フィルタ・親階層漏れ・GUID重複がないことを確認しました。
- 描画4テーマ×2ステージのD3D12 DXILを`dxv.exe`で検証し、全8出力が成功しました。
- 同じ4テーマ×2ステージのVulkan SPIR-Vを`spirv-val --target-env vulkan1.3`で検証し、全8出力が成功しました。SPIR-V検証は通常のビルドにも含まれます。
- `VULKAN_SDK`を外した環境でもD3D12三角形プロジェクト単体がビルドできること、Vulkan側はSDK導入方法の分かるエラーで停止することを確認しました。
- 共通`Common.hlsli`の更新時刻を変えて、両APIの頂点・ピクセルシェーダが再コンパイルされることを確認しました。ソース内容は変更せず、確認後に元の更新時刻へ戻しています。

## 実行確認

全10サンプルについて、Debug／Releaseの双方で以下を確認しました。

- 初期化、通常描画48回、640×480へのリサイズ。
- 最小化、800×600への復帰、描画継続。
- GPU読戻しによるPNG保存、正常終了、作業ディレクトリと異なる実行ファイル基準のアセット読込。
- DebugではD3D12 Debug Layer／Vulkan Validation Layerを有効化し、エラー0件。Vulkanでは同期検証（`VK_LAYER_VALIDATE_SYNC=1`）も有効にしています。

初期化時のSwapchain画像バリアは、Acquire Semaphoreと同じCOLOR_ATTACHMENT_OUTPUTステージをsource stageに含めて依存をつなぎます。同期検証でこのレイアウト遷移まで確認しています。

テクスチャ・モデル・シェーダの欠落を両APIで試し、対象ファイル名を含む診断、終了コード1、終了処理での検証エラー0件を確認しました。

## 画像確認

Docsの各API配下に800×600の実画像を5枚ずつ保存しています。三角形の頂点カラー、テクスチャの4色・上向き矢印、BOXの3面・深度、OBJの画像表示を目視確認しました。

両APIのPNGをRGBAの画素値で比較した結果は以下の通りです。PNGの圧縮バイト列ではなく、復号した画素の比較です。

| テーマ | 最大チャンネル差（8bit） | 差のある画素数 |
|---|---:|---:|
| Initialization | 0 | 0 |
| Triangle | 0 | 0 |
| Texture | 0 | 0 |
| Box | 0 | 0 |
| Model | 0 | 0 |

この環境では全5テーマでピクセルが一致しました。GPUやドライバが変わった場合の完全一致を保証するものではありません。

## CPU側テスト

`Tools/TestLoaders.ps1`で以下を検証しました。

- OBJの負インデックス、凸五角形の三角形化、UV境界と法線境界での頂点分割、V反転、同梱BOXの24頂点・36インデックス。
- 0／範囲外インデックス、UV欠落、法線の不正参照、頂点数不足、不正数値、面のないOBJに対する行番号付きエラー。
- WICのRGBA展開、行末パディングのある画像のPNG保存・再読込、赤と青のチャンネル保持。
- 画像・バイナリがない場合のファイル名付きエラー。

GPU結果の再確認は`Tools/TestSamples.ps1`、Docs画像の更新は`-CaptureDocs`、欠落ファイルの確認は`Tools/TestFailureCases.ps1`で実行できます。試験用ログ・バイナリは`.cache`に置き、Git管理から除外しています。
