# 外部依存関係

SDK以外の外部ライブラリは、このフォルダにGit Submoduleとして追加します。初回5テーマでは外部ライブラリを導入せず、画像読込にWindows標準WIC、数学処理にWindows SDKのDirectXMathを使用しています。

後続のDDS・Cubemapサンプルでは[DirectXTex](https://github.com/microsoft/DirectXTex)、GUIのテーマでは[Dear ImGui](https://github.com/ocornut/imgui)を導入する予定です。追加時は安定したコミットへ固定し、用途・ライセンス・更新方法をここへ記録します。

Agility SDK／D3D12用DXCはNuGet、Vulkan／Vulkan用DXCはVulkan SDKで管理します。SDKをSubmoduleとして追加する必要はありません。
