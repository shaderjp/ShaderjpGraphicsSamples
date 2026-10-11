# 外部依存関係

SDK以外の外部ライブラリは、このフォルダにGit Submoduleとして追加します。初回5テーマでは外部ライブラリを導入せず、画像読込にWindows標準WIC、数学処理にWindows SDKのDirectXMathを使用しています。

後続のDDS・Cubemapサンプルでは[DirectXTex](https://github.com/microsoft/DirectXTex)、GUIのテーマでは[Dear ImGui](https://github.com/ocornut/imgui)を導入する予定です。追加時は安定したコミットへ固定し、用途・ライセンス・更新方法をここへ記録します。

Agility SDK／D3D12用DXCはNuGet、Vulkan／Vulkan用DXCはVulkan SDKで管理します。SDKをSubmoduleとして追加する必要はありません。

## glTFローダーのJSON解析

[nlohmann/json v3.12.0](https://github.com/nlohmann/json/tree/v3.12.0)を`nlohmann-json`のSubmoduleとして導入し、タグの実体コミット`55f93686c01528224f448c19128836e7df245f72`へ固定しています。`single_include/nlohmann/json.hpp`だけをC++から参照し、[MITライセンス](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT)の著作権・許諾表示を保持します。JSON構文解析に使い、glTF構造の読込は教材用に自作しています。以前のcgltf採用案はこの構成に置き換えました。

サンプル06_Gltfだけがこのヘッダーを参照します。初回01〜05は参照しません。取得は`git submodule update --init --recursive`、更新は対象コミットを確認してcheckoutし、親リポジトリでgitlinkの変更を記録します。`git submodule update --remote`で意図せず最新版へ更新しないでください。実行ファイルの出力先へLICENSE.MITも配置します。[ローダー解説](../Docs/GltfLoading.md)を参照してください。

制作検証用のKhronos glTF-Validatorは固定リリースを`.cache`に取得し、配布サンプルのランタイム依存とは分けています。
