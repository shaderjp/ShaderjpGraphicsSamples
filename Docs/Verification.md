# グラフィックスサンプルの検証記録

検証日: 2026-10-11（日本時間）。結果は以下の実機・SDK・コンパイラの組合せに対するものです。

最新の追加結果は末尾の「第22・23回／発展10 GPUスキニング」を参照してください。

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

## サンプル06: glTF Albedoの追加検証

検証日: 2026-10-11。nlohmann/json v3.12.0をSubmoduleで導入し、コミット`55f93686c01528224f448c19128836e7df245f72`のシングルヘッダーを使用しました。LICENSE.MITを保持し、両APIの出力フォルダへコピーできることも確認しています。

環境は上表と同じVS 2026 Community 18.10.0、v145、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88です。今回確認した`cl.exe`は19.51.36257.0（ツールセット14.51.36257.0）。D3D12用DXCは1.9.2609.5／01b62ad47、Vulkan SDKは1.4.341.1、SDK用DXCは1.9.0.5180／e35541826です。

- MSBuildでNuGet復元、サンプル06を加えた全12プロジェクトのx64 Debug／Releaseビルドに成功。コンパイラの警告・エラー0件。
- `Tools/ValidateProjects.ps1`で全12組のプロジェクト／フィルタを検証。新規2プロジェクトはJSONヘッダー、LICENSE、glTF／BIN／PNG、Scene.hlsliまで分類済み。
- サンプル06のDXILのVertex／Pixelを`dxv.exe`で検証し成功。SPIR-VのVertex／Pixelも通常ビルド内の`spirv-val --target-env vulkan1.3`で成功。
- `Scene.hlsli`の更新時刻だけを変えて、両APIのVertex／Pixel計4出力が再コンパイルされることを確認し、更新時刻を復元。`VULKAN_SDK`を外した環境でDirect3D12_06_Gltf単体のビルドも成功。
- `Tools/TestLoaders.ps1`で既存OBJ／WIC／バイナリ試験に加え、同梱glTF Albedo／PBRの頂点・インデックスを検証。glTFのオフセット・interleaved stride・16/32-bit Index・座標と頂点順・UV非反転・材質の参照とfactor・percent escape／UTF-8 URIを確認。
- 不正Accessorの範囲・stride・型・件数、範囲外Index、非有限頂点、UV欠落、外部ファイル欠落、不正URI、ノード変換・拡張などの未対応機能、JSON構文エラーを拒否し、ファイル名／JSON Pointer（構文エラーはbyte位置）を確認。
- `Tools/TestFailureCases.ps1`で05／06の両APIについて、画像・モデル・ピクセルシェーダの欠落を確認。サンプル06はglTF読込前の失敗も正常に終了し、終了コード1・対象ファイル名・終了処理の検証エラー0件を確認。
- 全12サンプルでDebug／Releaseの通常描画48回、リサイズ、最小化／復帰、GPU読戻し、終了に成功。Debugは両APIの検証Layer有効、Vulkanは同期検証も有効、検証エラー0件。

[Direct3D12画像](Direct3D12/Images/06_Gltf.png)と[Vulkan画像](Vulkan/Images/06_Gltf.png)は実GPUの800×600画像です。Albedoだけが表示され、白い矢印が上向き、球と台座の前後関係がDepth Testで保たれることを目視確認しました。色・画像はsRGB、計算は線形、factorは既定の白です。両画像の比較は最大チャンネル差1／差のある画素1個で、この環境ではごく小さな補間丸めの差でした。

PBR照明、Normal／ORM／EmissiveのGPU使用、複数モデル・材質や汎用glTF対応はこの回の検証範囲に含めません。Blenderの参考画像とアセット検証は[アセット制作の検証記録](Assets/Verification.md)に分けています。


## サンプル07・08：ライティングからPBRへの追加検証

検証日：2026-10-11。07_Lightingと08_PBRを各APIへ追加し、ソリューションは8テーマ・16プロジェクトになりました。検証環境は上表と同じVS 2026 Community 18.10.0、v145、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88です。cl.exeのFileVersionは19.51.36257.0、ProductVersionは14.51.36257.0。Agility 1.619.6、D3D12 DXC 1.9.2609.5／01b62ad47、Vulkan SDK 1.4.341.1、付属DXC 1.9.0.5180／e35541826を使用しています。

- NuGet復元、全16プロジェクトのx64 Debug／Releaseビルドに成功。最終ビルドはコンパイラ警告・エラー0件。
- `Tools/ValidateProjects.ps1`で全16組のXML、項目対応、フィルタ階層、GUID、ファイル実在を検証。新規4プロジェクトは共通Lighting.h／Lighting.hlsli、08の追加画像も分類済み。
- 07／08の各Vertex／PixelのDXILとSPIR-VをDebug／Release両方で検証。dxvの8出力、spirv-valの8出力が成功。
- `Lighting.hlsli`の更新時刻を変更し、4プロジェクト×2ステージのDebug出力計8本が再コンパイルされたことを確認後、時刻を復元。VULKAN_SDKを外した環境でD3D12_08_PBRのRelease単体ビルドも成功。
- SPIR-Vを逆アセンブルし、Sceneのメンバーoffsetが0／64／128／192／208／224／240／256／272／288／304であることを確認。CPUの320-byte配置と一致。
- `Tools/TestLoaders.ps1`で既存の試験と、PBR画像参照・既定値・係数・Sampler共通化・ORM同一参照・UV0・接線の有無／三角形内の符号・画像欠落の成功／失敗ケースが通過。06／07の読込で追加PBR画像パスが未使用であることも確認。
- `Tools/TestSamples.ps1`をDebug／Releaseで全16本に実行。各48回の通常描画、リサイズ、最小化／復帰、GPU読戻し、終了が成功。Debugは両APIの検証LayerとVulkan同期検証を有効化し、検証エラー0件。
- `Tools/TestLighting.ps1`をDebug／Releaseで実行。各APIの07の3モードと08の8モードを起動・取得。一定色のPNGと既知の法線・接線を持つ平面で、Albedo／Metallic／Roughness／AO／Emissive／World Normalと、07のAlbedo／LambertをCPUで求めた期待画素に照合。sRGB・ORMチャンネル・材質係数・Normal Scale・接線基底・線形照明・Reinhardを確認し、許容差は8bit RGB各2以内。
- `Tools/TestFailureCases.ps1 -Topics 07_Lighting,08_PBR`で両APIの画像・glTF・Pixel Shader欠落時のファイル名、終了コード1、終了処理の検証エラー0件を確認。

Docs画像はTestSamples／TestLightingのCaptureDocsで取得した800×600の実GPU画像です。Lambertの曲面の明暗、Blinn-Phongの白いハイライト、08の金属色のハイライト・粗さ領域・Normal Mapの表面模様・赤いEmissive、Normal Mapを外した比較、材質確認表示を目視確認しました。光は固定し、比較時のモデル・カメラ条件を揃えています。

各APIで07／08のAlbedo確認表示と06_GltfのPNGをRGBAの画素で比較し、完全一致しました。両API間の比較は次の通りです。PNGの圧縮データではなく、復号したRGBAを比較しています。

| 表示 | 最大チャンネル差（8bit） | 差のある画素数 |
|---|---:|---:|
| 07 Blinn-Phong（既定） | 1 | 6 |
| 07 Lambert | 1 | 5 |
| 07 Albedo | 1 | 1 |
| 08 PBR（既定） | 1 | 5 |
| 08 World Normal | 1 | 4 |
| 08 Metallic | 1 | 1 |
| 08 Roughness | 0 | 0 |
| 08 AO | 1 | 4 |
| 08 Emissive | 0 | 0 |
| 08 PBR without Normal Map | 1 | 4 |
| 08 Albedo | 1 | 1 |

この環境では差はごく少数の補間丸めでした。異なるGPU／ドライバでの完全一致を保証する比較ではありません。方向光1本、1 mip、単一材質・共通Sampler・ORM画像を対象とし、IBL、Shadow Map、複数光源、Deferred Renderingはこの回には含めていません。常時回転する光は比較キャプチャでは無効です。

## サンプル09：Dear ImGuiと動的Constant／Uniform Buffer

検証日：2026-10-12。08のPBRを使う09_ImGuiを両APIへ追加し、9テーマ・18プロジェクトになりました。Dear ImGui v1.92.9bを`ThirdParty/imgui`のコミット`f1cc2ae15e53a861a874c3034aae6798fde194ab`へ固定しました。Submodule内の変更は0件です。本体4 cppと公式Win32バックエンドを使用し、GPU描画はAPIごとのImGuiRenderer.cppと事前コンパイルした共通HLSLです。

今回確認した環境はVS 2026 Community 18.10.0（18.10.12201.205）、v145、cl.exe 19.51.36257.0／ProductVersion 14.51.36257.0、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88です。Agility SDK 1.619.6、D3D12 DXC 1.9.2609.5／01b62ad47、Vulkan SDK 1.4.341.1、付属DXC 1.9.0.5180／e35541826を使いました。HLSLはSM6.6／HLSL 2021、Vulkanのターゲット環境は1.3です。

- NuGet復元後、全18プロジェクトのx64 Debug／ReleaseのMSBuildビルド・再構築に成功。最終コンパイラ警告・エラー0件。
- `Tools/ValidateProjects.ps1`で全18組のXML、ファイル実在、項目の過不足・重複、フィルタの宣言・親階層・GUIDを確認。09はD3D12が47項目、Vulkanが46項目。Dear ImGuiのソース・ヘッダー・Win32バックエンド、UI HLSL、ライセンスも分類済み。
- 09のモデル用2ステージとUI用2ステージをDebug／Releaseで事前コンパイル。DXILの計8出力をdxv、SPIR-Vの計8出力をspirv-valで検証して成功。
- UIのUi.hlsliの更新時刻を変更し、両APIのUI用Vertex／Pixel計4出力が再コンパイルされることを確認後、時刻を復元。D3D12_09_ImGuiはVULKAN_SDKを空に指定したRelease単体ビルドも成功。
- UI用SPIR-Vを逆アセンブルし、行列がset 0／binding 0、offset 0、MatrixStride 16であることを確認。UIの64-byte行列とシーンの320-byte定数は別のBufferで、どちらもGPU完了後に更新します。
- `Tools/TestLoaders.ps1`で既存OBJ／glTF／WIC／バイナリ、PBR材質・接線・定数のCPUテストが通過。
- `Tools/TestSamples.ps1`で全18本をDebug／Releaseで実行。各48回の通常描画、640×480への変更、最小化・800×600への復帰、GPU読戻し、終了が成功。09の最終変更後も両構成で再確認。DebugはD3D12 Debug Layer／Vulkan Validation LayerとVulkan同期検証を有効化し、検証エラー0件。
- `Tools/TestImGui.ps1`を両構成で実行。実ウィジェットへマウス位置・ボタンの入力イベントを与え、Intensityを3.00→10.67へ編集、Reset、再編集。CPUのパラメータへ直接代入して試験を成立させる処理はありません。
- UIを除いた右側の3D領域を比較すると、両API・両構成とも68,555画素でRGB各2より大きい変化があり、GPU側でライトの変更が反映されました。Reset後の3D領域は各API・構成の元画像と画素単位で完全一致。UI文字列だけが変わったケースでは通らない試験です。
- `Tools/TestFailureCases.ps1 -Topics 09_ImGui`で両APIのモデル・PNG・モデル用Pixel Shader・UI用Pixel Shaderの欠落を検証。対象ファイル名、終了コード1、部分初期化後の解放、検証エラー0件を確認。
- Dear ImGuiのMIT LICENSE.txtを両API・両構成の出力先へコピーし、Submoduleのライセンスとバイト単位で一致することを確認。

同じ既定値を使った両APIの3D領域（x=350〜799、800×600画像）の比較結果です。UIに表示されるAPI名は比較から除外しています。

| 構成 | 最大チャンネル差（8bit） | 差のある画素数 |
|---|---:|---:|
| Debug | 0 | 0 |
| Release | 1 | 1 |

添付画像は[Direct3D12の通常表示](Direct3D12/Images/09_ImGui.png)／[強度変更後](Direct3D12/Images/09_ImGui-intensity.png)、[Vulkanの通常表示](Vulkan/Images/09_ImGui.png)／[強度変更後](Vulkan/Images/09_ImGui-intensity.png)です。通常表示はTestSamples -CaptureDocs、比較画像はTestImGui -CaptureDocsで取得しました。すべて800×600の実GPU画像で、パネルの文字・フォント・クリッピング、球と台座、Normal Map、光の強度による明るさの変化を目視確認しました。

09のUI描画は3D用Depth Bufferを使用しません。D3D12はDSVを外してUI用PSOへ切替え、Vulkanは明示的なカラー依存のBarrierとLOADを使う二つ目のDynamic Rendering Scopeに分けています。固定フォントアトラス1枚、単一Viewport、1フレームずつ完了を待つ構成を対象とし、動的フォント・ユーザー画像・複数フレームの並行実行は今回の範囲に含めません。

## 全サンプルの初期解像度を1280×720へ変更

検証日：2026-10-12。全9テーマ・両APIの18プロジェクトの初期クライアント領域を1280×720へ変更しました。`Common/Application.cpp`の`DefaultClientWidth`／`DefaultClientHeight`を、初期状態、ウィンドウ作成、最小化からの復帰で共用します。初期化ログは実際の幅と高さを記録します。途中の640×480への変更はリサイズ検証用に維持しています。

環境はVS 2026 Community 18.10.0（18.10.12201.205）、v145／MSVC 19.51.36257.0、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88です。Agility SDK 1.619.6、D3D12 DXC 1.9.2609.5／01b62ad47、Vulkan SDK 1.4.341.1／付属DXC 1.9.0.5180／e35541826を使用しました。

- 全18プロジェクトのx64 Debug／ReleaseをMSBuildでビルドし、警告・エラー0件。`Tools/ValidateProjects.ps1`で全18組のプロジェクト／フィルタXMLの検証が成功しました。
- `Tools/TestLoaders.ps1`のOBJ／glTF／WIC／ファイル読込・材質・定数のCPU検証が成功しました。
- `Tools/TestSamples.ps1`を両構成で実行。全18本で48フレーム描画、1280×720から640×480への変更、最小化・1280×720への復帰、終了が成功しました。計36枚のGPU読戻しPNGが1280×720であることも確認しました。DebugはD3D12 Debug Layer／Vulkan Validation Layer・同期検証を有効にし、両構成の検証エラーは0件です。
- `Tools/TestLighting.ps1`の参照画素を画像の幅・高さから求める中心へ変更し、両構成・両APIで全表示モードと定数テクスチャのGPU参照値の検証が成功しました。
- `Tools/TestImGui.ps1`の画像比較領域を1280×720へ変更し、両構成・両APIでスライダー編集、Reset、再編集を確認しました。UIを除く領域でDirect3D12は184,860画素、Vulkanは184,861画素が変化し、Reset後はそれぞれの元画像と完全一致しました。
- 確認用のGPU画像は`.cache`へ保存しました。1280×720のPBR・ImGui表示で、パネルの文字・クリッピング、球と台座の奥行きと描画領域を目視確認しました。
- 既存のDocs・Shader.jp記事画像77点は変更前後のSHA-256が一致しました。上の各節の800×600の撮影条件・比較値は過去の記録として維持し、既存の画像は更新していません。
- Shader.jpの各API第1〜8回、第0回、目次、初期化比較の説明を更新しました。固定コミットを使う場合の解像度変更手順を第0回に記載し、画像の撮影条件と区別しています。Astroの63ページ・Pagefindの29記事をビルドし、グラフィックス関連22記事の内部リンク・アンカー736件を確認しました。

## 第10〜13回 Lighting LabとCubemap・IBL・Shadow map

検証日：2026-10-12（日本時間）。第10 Cubemap、第11 Diffuse IBL、第12 Specular IBL、第13 Shadow mapを両APIへ追加しました。アプリケーションは13テーマ・26本、CPU画像処理用のDirectXTexCpuを含めて27プロジェクトです。

環境はVS 2026 Community 18.10.0（18.10.12201.205）、v145／C++20、cl.exe FileVersion 19.51.36257.0、Windows SDK 10.0.26100.0、NVIDIA GeForce RTX 4070／NVIDIA 610.88（32.0.16.1088）です。Agility SDK 1.619.6、D3D12用DXC 1.9.2609.5／01b62ad47、Vulkan SDK 1.4.341.1、SDK用DXC 1.9.0.5180／e35541826を使いました。HLSLはSM6.6／HLSL 2021、SPIR-Vの対象はVulkan 1.3です。

Blenderは5.2.1 LTS、build hash 9e2066aef7efです。DirectXTex may2026を4feb3e11a020f35b796fc769a74216a555d4f5efへ固定し、vendorソースを変更せずCPUのDDS読込に使用しました。Dear ImGui v1.92.9b／f1cc2ae15e53a861a874c3034aae6798fde194abも変更していません。

- NuGet復元、全27プロジェクトのx64 Debug／Releaseビルドが成功。最終ビルドのコンパイラ警告・エラー0件。
- Tools/ValidateProjects.ps1で27組のvcxproj／filtersのXML、ファイル実在、項目の一対一対応、フィルタ宣言・親階層・GUIDを検証。追加のScene・DDS・Manifestも分類・配布対象へ登録済み。
- 新規4テーマのモデル、Skybox、UI、ShadowのDXIL計50出力をdxvで検証して成功。対応するSPIR-V計50出力はビルド内のspirv-val --target-env vulkan1.3で成功。
- 第13回のVertex SPIR-Vを逆アセンブルし、Sceneのoffset 0／64／128／192／208／224／240／256／272／288／304／320／384／448／464を確認。CPUの480-byte配置と一致。D3D12の格納は512-byte、VulkanはminUniformBufferOffsetAlignmentへ切り上げています。
- Tools/TestLightingLabAssets.ps1で全17モデルをglTF-Validator 2.0.0-dev.3.10により検証し、エラー・警告0件。合計9,684三角形。保存済み正本から一時フォルダへ再書出しし、JSON・BIN・参照PNGが一致、blendのSHA-256も変化なし。
- 環境は2048×1024の線形EXR、環境Cube 512×512×6、Irradiance 32×32×6、GGX Prefilter 256×256×6・9 mip、BRDF LUT 256×256。HDR値の保持、面順、mip数、有限値とManifestの配布ハッシュを確認。
- 固定Hammersley列の1024 samplesで、一定色Cの環境に対するIrradianceがπC、粗さ別Prefilterが全段階でCを保つことを数値検証。DDSはRGBA16F／RG16F、生成と読込でNaN／Infを拒否。
- Tools/TestLightingLab.ps1をDebug／Releaseで実行。CPUのDDS面・mip・byte配置、Sceneの右手系→左手系、遮蔽物のWorld変換、不正DDSの拒否が成功。
- 両API・両構成で環境切替、Direct／Diffuse／Specularの寄与、Shadowなし／硬いShadow／3×3 PCF／深度表示、遮蔽物移動、実ウィジェットの編集・Resetを検証。Debugは各APIの検証LayerとVulkan同期検証を有効化し、検証エラー0件。
- Shadowの有無で11,191画素がRGB各2を超えて変化。遮蔽物の移動で21,223画素が変化し、移動前後の遮蔽物本体の投影範囲を除いても2,067画素が変化しました。両API・両構成で同じ値となり、物体本体の移動だけで成立する試験ではありません。
- 各テーマのUIはEnvironment intensityを入力イベントで編集。UIを除いたシーン画素の変化と、Reset後の基準画像への完全一致を確認。GPU完了待ちの後に定数・UI Buffer・Descriptorを更新しています。
- Tools/TestSamples.ps1を両構成で全26本へ実行。描画、640×480への変更、最小化・1280×720への復帰、GPU読戻し、終了が成功。Tools/TestLoaders.ps1と既存09のTools/TestImGui.ps1の両構成も通過。
- Tools/TestLightingLabFailures.ps1で両API・両構成のScene／DDS／Pixel Shader欠落を検証。対象ファイル名のあるエラー、終了コード1、部分初期化の解放、検証エラー0件を確認。欠落は隔離した配布フォルダで作り、通常の実行データへ影響を与えていません。

新規記事用PNGは45点、すべて1280×720です。実GPUの各寄与・環境切替・粗さ比較・投影するShadow・深度・UIと、Cyclesの環境のみ／方向光のみ／合成の参考画像を確認しました。既存のキャプチャ・Shader Ball配布データ・編集正本の計64ファイルはGitの元データとハッシュが一致しています。

Cycles参考画像はCPU、128 samples、seed 0、denoise有効、AgX／Medium High Contrastです。環境のみと方向光のみは最大バウンス数1、合成は8です。Principled BSDF、局所的な光の伝播、表示変換の違いがあるため、リアルタイム画像と画素の一致を求める比較にはしていません。

この追加は静的環境のIBLと方向光1本のShadowを対象にします。動的Reflection Probe、SSR、レイトレーシング反射、Cascaded Shadow Map、点光源Shadow、面光源の半影は含めていません。新規記事と画像はリポジトリのDocsへ保存し、サイトへの反映・公開・pushは行っていません。

## 第14〜16回 HDR Render-to-Texture・Compute Bloom・GPUパーティクル

検証日：2026-10-12（日本時間）。両APIの独立した実行プロジェクト6本を追加しました。合計16テーマ・32アプリ、DirectXTexCpuを含め33プロジェクトです。

検証環境はVS 2026 Community 18.10.0（18.10.12201.205）、v145／C++20、cl.exe 19.51.36257.0、Windows SDK 10.0.26100.0、NVIDIA GeForce RTX 4070／NVIDIA 610.88です。Agility SDK 1.619.6、D3D12 DXC 1.9.2609.5／01b62ad47、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180／e35541826、SM6.6／HLSL 2021、Vulkan 1.3を維持しました。

- NuGet復元、全33プロジェクトのx64 Debug／Releaseビルド、SPIR-V検証が成功。最終ビルドの警告・エラー0件。新規6本のDXIL計80出力（両構成）もdxvで検証しました。
- `Tools/ValidateProjects.ps1`で33組のvcxproj／filtersをXML解析し、全項目の一対一対応、フィルタ宣言、親階層、GUIDとファイル実在を確認しました。MonitorのglTF・BIN・PNG・Scene・Manifestと追加HLSL・共有ヘッダーも分類済みです。
- `Tools/TestPostProcessAssets.ps1`でMonitorをglTF-Validator 2.0.0-dev.3.10により検証。エラー・警告0件、1 Node・1 Mesh・1 Primitive・1 Material、12三角形です。Blender 5.2.1 LTS／9e2066aef7efの保存済み正本からJSON・BIN・参照PNGが一致し、正本のSHA-256も変化しませんでした。
- `Tools/TestLoaders.ps1`と拡張したLightingLab CPU試験が両構成で成功。旧Sceneの既定値、新Sceneの第二カメラとモニター指定、右手系から左手系への変換、不正FOV／同じ位置とTargetの拒否を確認しました。
- `Tools/TestSamples.ps1`をDebug／Releaseで全32本へ実行。48フレームの描画、640×480、最小化・1280×720への復帰、GPU読戻しPNG、終了が成功しました。Debugは両APIの検証Layer、Vulkanの同期検証を有効にし、検証エラー0件です。
- `Tools/TestPostProcess.ps1`が両API・両構成で成功。Final／Grayscale／Monitor／HDR input／Extract／Horizontal／Bloom、StudioとRoom、箱の移動による別カメラ映像の更新を確認しました。512×512の診断表示は縦横比を保持します。
- 第14回の線形HDR読戻しで、メイン画像の最大RGBは72.875、別カメラは24.453125でした。旧Exhibition.jsonを使った第13回と第14回の表示差は、両API・両構成とも8bitで最大1。表示変換の位置を移しても、従来の照明を維持しています。
- 第15回は黒入力を黒のまま維持し、一定色[4, 2, 0.5]・Threshold 1から抽出[3, 1.5, 0.375]を確認。横・縦のフィルタ後もFP16精度内（絶対差0.003以内）で一定色を保持しました。端の単一HDR画素を独立したCPU計算と照合し、逆側の端の汚染がないことも確認しました。
- 641×479へのリサイズで半解像度は321×240となり、画像外のスレッドと2×2／9 tapの端処理が成功。読戻した全HDR画像にNaN／Infはありませんでした。
- 第16回は257個の粒子の7回のEuler更新を、初期位置・速度・年齢から独立して計算した値と照合（絶対差0.00002以内）。257以降の未使用領域は初期Bufferと完全一致。最小1個、最大65,536個でも同じ先頭粒子の更新と有限値を確認し、1個の場合の未使用領域も一致しました。
- 実ウィジェットへ入力してPause中のStep、Reset、Pause解除を試験。Stepは1回だけ更新し、Reset後の全容量BufferとUI外のシーン画素は基準と完全一致しました。同じGPU BufferをStorage／UAVとInstance用頂点入力へ接続しています。通常フレームに粒子のCPU読戻し・再転送はありません。
- 第14・15回の実スライダーとReset、既存09の`Tools/TestImGui.ps1`、第10〜13回の`Tools/TestLightingLab.ps1`も両構成で成功しました。
- `Tools/TestPostProcessFailures.ps1`でPostPixel／Extract／ParticleUpdateの欠落を隔離した配布フォルダで作り、両API・両構成で対象ファイル名のあるエラー、終了コード1、部分初期化の解放、検証エラー0件を確認しました。

Bloomを合成する前と後のPNGで、RGBの最大差が2を超えた画素数です。Studio、同じカメラ、露出1、Threshold 1、Strength 0.15、UIなしで比較しました。

| 構成 | Direct3D 12 | Vulkan |
|---|---:|---:|
| Debug | 3,865 | 3,865 |
| Release | 3,863 | 3,865 |

新規Docs画像は24点、すべて1280×720。第14回のモニターとGrayscale、第15回の抽出・横・縦の途中画像、第16回の火花と257粒子の比較を実GPUから撮影しました。標準条件はStudio、直接光3、環境強度1、露出1。完成した火花は48 frames、それ以外は8 frames、固定dt 1／60、固定Hash seedです。モニター移動比較だけOccluder Xを1へ変えています。

今回の変更前から存在する画像・DDS・EXR・glTF・BIN・Blender正本217ファイルはGitの元データとバイト単位で一致しています。第三者のSubmoduleソースも変更していません。

Shader.jp向けにはAPI別6記事と共通章・モニター制作章をローカルで準備しました。Astroの91ページ、Pagefindの50記事、内部リンク・アンカー4,456件、24枚の新規画像とソースのSHA-256を検証しています。記事の準備時点では第14〜16回のコードを公開準備版として明記し、存在しない公開ファイルへのリンクは作っていません。Shader.jpへのサイト公開は別工程です。

SSAOは第17回の候補です。Async Compute、粒子のソート・衝突、Indirect Drawは後の拡張とし、今回のGPU粒子は加算合成・深度テストあり・深度書込みなしです。

## Workshopアセットの先行制作

Deferred Renderingと、その後のレイトレーシング・Mesh Shaderで共用するWorkshopを制作しました。標準251,200三角形と負荷比較2,260,800三角形、保存済みBlender正本、共有glTF、環境・IBL、固定カメラ、1280×720のCycles参考画像を配布します。[制作仕様](Assets/Workshop.md)と[アセットの検証記録](Assets/WorkshopVerification.md)を参照してください。

この工程はアセットと読込・接続仕様までです。旧ローダー、C++／HLSL、プロジェクトとSDK設定は維持し、拡張CPUローダー・Deferred・Meshlet・GPU描画試験は後続工程へ引き継ぎます。


## 第17・18回 Deferred Rendering

Workshopを拡張CPUローダーで読み、PS／CSで同じ18 B／画素のG-bufferをシェーディングする4本を追加しました。Shadow、PBR、IBL、HDR、Bloom、Forward透明合成と診断表示、GPU時間を統合しています。位置はDepthから再構築し、位置用ターゲットは作っていません。

37プロジェクトのfilters・Restore・Debug／Releaseビルド、全36サンプルのライフサイクル、旧ローダー、Workshop CPU、Deferredの数値・UI・Shader欠落試験が成功しました。検証エラーは0件です。PS／CSのG-buffer・Depthは一致し、HDR差は規定内でした。Releaseの固定経路1200 frames×8条件を測定し、1280×720の新規画像18枚を確認しました。詳細と計測データは[Deferred検証記録](DeferredVerification.md)を参照してください。


## 2026-10-13 第19・20回：Tiled DeferredとClustered Shading

追加4本のDebug／Release、全プロジェクトとfilters、SPIR-V、旧ローダーとWorkshop CPU試験が成功しました。第1〜18回の両API 36本のライフサイクルも再確認しました。All／Tiled／ClusteredのG-buffer一致、HDRの許容誤差、ライトリストの独立した球／平面検証、4,680ライト、容量超過、641×479、実UIのReset、実行中のモード切替を確認し、検証エラーは0件でした。Releaseの1200 frames×12条件を測定しました。[詳細記録](LightCullingVerification.md)を参照してください。

## 2026-10-13 発展編01〜03：DXR／Vulkanレイトレーシング

Albedo一次レイ、BRDFと方向光・点光源の影、1バウンスの鏡面反射を、両APIの独立した6プロジェクトとして追加しました。Workshopの共有PrimitiveをBLASへ、配置をTLASへ接続し、OPAQUEとMASKを処理します。API初期化・加速構造・State Object／Pipeline・Shader Table／SBT・同期・解放は各サンプルにあります。

VS 2026 Community 18.10.12201.205、v145／cl.exe 19.51.36257.0、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88、Agility SDK 1.619.6、NuGet DXC 1.9.2609.5、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180で検証しました。SM6.6／HLSL 2021／Vulkan 1.3を維持しています。

Restore、ソリューションのDebug／Release、filters、DXIL／SPIR-VとCPU配置、専用GPU試験、旧ローダーとWorkshop CPU、全サンプルのライフサイクル、既存09 ImGuiと17・18 Deferredの回帰試験が成功しました。既知UV、MASKの表裏・影・反射、ライトまでの距離制限、画面外の反射先とその影、環境Miss、反射OFF、Stress、641×479、実UIとReset、Shader欠落と部分解放を確認し、検証エラーは0件でした。新規画像20点はすべて1280×720の実GPUキャプチャです。[発展編目次](AdvancedGraphicsSamples.md)・[詳細記録](RaytracingVerification.md)を参照してください。

## 2026-10-13：ソリューションフォルダと第13回以降のカメラ操作

- 全46 APIサンプルをDirect3D12／Vulkanのソリューションフォルダへ配置。`ValidateProjects.ps1`で全47プロジェクトのXML／filters、ソリューションとプロジェクトのGUID一致、フォルダ割当を検証した。
- VS Community 2026 18.10.0、v145／MSVC 19.51.36257、Windows SDK 10.0.26100.0、D3D12 Agility 1.619.6／DXC 1.9.2609.5、Vulkan SDK 1.4.341.1。GPUはNVIDIA GeForce RTX 4070、ドライバ32.0.16.1088（610.88）。
- Restore成功。ソース確定後の全ソリューションDebug Build／Release Rebuildが成功し、最終ビルドのコンパイラエラー・警告は0件。SPIR-V検証もビルド内で成功した。途中のヘッダー更新と重なったビルドの実行結果は完了判定へ含めていない。
- `TestCameraNavigationCpu.ps1`成功。左手系の前進、斜め移動の速度、Shift、UI入力取得、フォーカス・キャプチャ喪失、短いドラッグ、上下回転の制限、経路からのFOV／Near／Far引継ぎ、Resetを確認した。
- `TestCameraNavigation.ps1`をDebug／Releaseで実行。第13〜20回とAdvanced01〜03の両API、計22サンプル×2構成で成功した。実際のWin32キー／マウスメッセージを送り、移動と回転で各1,000画素以上のRGB変化、Rによる初期画像への完全一致を確認した。全撮影は1280×720、検証エラー0件。
- 移動前・移動後・回転後・Reset後について、第19回のAll／Tiled／Clusteredモードを追加比較した。両APIとも各表示画像のRGB差は0だった。G-buffer、深度再構築、ライトの絞り込みが同じ操作後の視点を使うことを確認した。
- `TestSamples.ps1`をDebug／Releaseで全46サンプルへ実行。描画、1280×720撮影、640×480リサイズ、最小化から1280×720へ復帰、終了が成功し、全実行で検証エラー0件。`TestLoaders.ps1`も成功した。
- 既存記事画像と配布アセットは変更せず、操作試験の画像は`.cache/camera-navigation/<構成>`へ保存した。Lighting LabとWorkshopの回転後画像を目視確認した。操作方法は[CameraNavigation.md](CameraNavigation.md)と各サンプルの解説へ記録した。

## 2026-10-14 発展編04〜09：Mesh ShaderとGPU Driven Rendering

04のAlbedoのみのMesh Shader、meshoptimizerの共通章、05のCompute＋Indirect、06のAS／TaskによるMeshlet Culling、07の方向光Shadow Map、08のDeferred、09のTiled Deferredを追加しました。両APIの独立した12アプリに、移動・回転・カメラResetを備えます。meshoptimizer v1.3は`9e1f07b159d3cb777f1c67ed31fc11fd117986f4`へ固定し、上流ソースは変更していません。共通章ではDXR Part 2のCLAS／Cluster BLAS／TLASとMeshletの形状分割のつながりを説明しています。

VS 2026 Community 18.10.12201.205、v145／MSVC 19.51.36257.0、Windows SDK 10.0.26100.0、RTX 4070／NVIDIA 610.88、Agility SDK 1.619.6、NuGet DXC 1.9.2609.5、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180で検証しました。

Restore、全60プロジェクトのDebug／ReleaseビルドとXML／filters、DXIL／SPIR-VとCPU配置、旧ローダー・Workshop・MeshletのCPU試験が成功しました。全58アプリのライフサイクル、新規12アプリのカメラ操作、GPUの可視ID・件数とHDR、MASKの表裏と影、画面外Caster、All／Tiled／容量超過、4,680ライト、UI編集・Reset、Shader欠落と部分解放を両構成で確認しました。Stressの20ケースでも641×479、最小化・1280×720復帰とGPU数値照合が成功しています。既存ImGui、Deferred、Tiled／Clustered、レイトレーシングの回帰も通過し、検証エラーは0件でした。

API別12記事と共通章、1280×720の実GPU画像52枚を確認しました。Releaseの固定経路1,200フレーム×52条件の性能をJSON／CSVへ保存し、GPU TotalとCPUのコマンド記録を区別して報告しています。05のIndirectはCPU記録を減らしましたが、カリングONがGPUで遅い条件もあります。09のStressではAll→TiledのGPU中央値がD3D12で17.9732→2.0480 ms、Vulkanで17.3172→2.2331 msでした。条件、許容誤差と生データは[Mesh Shader検証記録](MeshShaderVerification.md)、記事の入口は[発展編目次](AdvancedGraphicsSamples.md)です。

## 2026-10-15：GPUスキニング用Silver Robotアセット

Aポーズの白銀ヒューマノイドをBlender 5.2.1 LTS（build hash 9e2066aef7ef）で制作しました。53ボーン、指の各節のウェイト、剛体装甲と柔軟部のブレンド、core glTF／GLBと自作の待機・その場歩行Actionを保存しています。編集・書出し方法は[制作仕様](Assets/SilverRobot.md)を参照してください。

Khronos glTF Exporter 5.2.40、glTF-Validator 2.0.0-dev.3.10、Blender付属Python 3.13.13／NumPy 2.3.4で検証しました。glTF／GLBのエラー・警告は0件。31,138出力頂点、44,584三角形、1スキン、5材質を確認し、CPUスキニングとBlenderの全頂点の変形差は最大7.8755e-7 mでした。独立した一時フォルダ内の保存済み正本から再出力したglTF／BIN／GLB／人型対応表は配布物とバイト単位で一致しました。1280×720のCycles CPU参考画像5点を確認しています。[詳細記録](Assets/SilverRobotVerification.md)と[機械可読の結果](Assets/SilverRobotValidation.json)に値を保存しました。

今回はモデル制作の工程で、描画サンプル・ローダー・Visual Studioプロジェクトは追加していません。外部モーションは同梱せず、QuaterniusのCC0ライブラリを今後のリターゲット候補として調査しました。

## 第21回 Visibility Buffer（2026年10月15日）

両APIの独立した2プロジェクトを追加し、全62プロジェクトのXML／filtersとソリューションの60 APIサンプルのフォルダ、Restore、x64 Debug／Release、DXIL／SPIR-V、既存CPUローダーを検証しました。ID・Depth・参照範囲、独立した重心座標・UV勾配、Near面、MASK／両面／BLEND、影、641×479、カメラの移動・回転・Reset、リサイズ・最小化復帰・終了が成功し、Debug検証エラーは0件です。

1280×720の実GPU画像14枚と、第19・20回、第21回Tiled／Clusteredの16条件の比較CSV／JSONを保存しました。条件と結果は[Visibility Buffer検証記録](VisibilityVerification.md)にまとめています。

## 第22・23回／発展10 GPUスキニング（2026-10-15）

新規6実行プロジェクトを追加し、両APIの基本23＋発展10で計66アプリ、CPUライブラリ2本を含むソリューションをRestore・x64 Debug／Releaseでビルドしました。v145／Windows SDK 10.0.26100.0を維持し、C++／HLSLの警告・エラー0件。vcxproj／filtersのXML整合とVulkan SPIR-V検証も成功しています。

実測環境はVisual Studio Community 2026 18.10.0、cl 19.51.36257（tool directory 14.51.36231）、MSBuild 18.10.1-1.26427.6、NVIDIA GeForce RTX 4070、driver 610.88、Windows 11 Pro 10.0.26300です。D3D12はAgility 1.619.6／DXC 1.9.2609.5（01b62ad47）、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180（e35541826、DLL表示1.10）、SPIRV-Tools v2026.1／target Vulkan1.3を使用しました。

別正本SilverRobotAnimated.blendに固定SubmoduleのUniversal Animation Library Standardから6クリップをリターゲットし、A/T確認ポーズも保存しました。ロボットのMITとQuaterniusモーションのCC0を分けています。glTF／GLBのKhronosエラー・警告0、保存済みblendからのバイト一致、全8 ActionのBlenderと独立CPU評価の最大位置差1.01178e-6 mを確認しました。

新規GPU試験は全クリップ、1体／64体、Computeの実Vertex BufferとCPU参照、VS／CS／MSのDepth・Shadow・HDR、動的境界のカリングON/OFF、画面外Caster、Loop／終端／速度／Seek／Reset、奇数リサイズを両構成で確認しています。全66アプリのライフサイクル回帰、旧ローダー、09のImGui、新6アプリのカメラ、Shader欠落時の終了も成功し、Debug Validationエラー0件です。

1280×720の実GPU画像、CPU／GPU比較JSON、Release 1体／64体の16条件の性能測定を追加しました。CPU姿勢・境界、CS変形、Shadow、Color、GPU Total、キャッシュの使用／確保容量を記録しています。Shader間のRaster順序による少数のHDR成分差も省略せず、[GPUスキニング検証記録](SkinningVerification.md)へまとめました。[ローダー記事](GltfSkinningLoading.md)と[GPU共通解説](GpuSkinning.md)を参照してください。

## GPUスキニングのユーザー確認と動画掲載（2026-10-15）

ユーザーによるローカル動作確認が完了し、OKの連絡とGitHub反映の指示を受けました。初回掲載では提供された6本の1280×720 MP4を確認し、README、API別6記事、glTFローダー記事、GPU共通章、Blenderアセット記事から参照できるようにしました。その後、同日の追加録画を含む8本へ更新しています（本ページ末尾）。[動画一覧](Assets/Videos/README.md)と[元ファイル名・SHA-256の記録](Assets/Videos/Manifest.json)は最新の8本に対応しています。

## 6サンプルのRoot motion ON／OFF（2026-10-15）

ImGuiにRoot motionを追加し、初期値とResetをOFFにしました。Walk／Runのその場モーションから支持足の後退速度を調べて前進量を復元し、Actor Worldへ適用します。Shaderの変形データはローカル空間のまま共有し、Shadow・カリングのWorld変換とライト範囲を移動へ追従させています。配布モデル・Blenderと固定モーション素材のバイト列は変更していません。

全体のx64 Debug／Releaseビルド、vcxproj／filters照合、CPUスキニング・旧ローダー、09のImGui回帰、新6アプリの描画・リサイズ・最小化復帰・終了を検証しました。新6アプリの実Checkbox操作を両構成で行い、ON時のDepth変化、OFFで元配置へ戻ること、ResetでRoot motionがOFFとなりHDRがbaselineへ完全復帰することを確認しています。周期をまたいだ前進、速度・停止、3方式と64体の動的カリングの比較も通過し、Debug検証エラー0件です。初期値OFFのHDRは更新前Releaseの全6アプリとビット一致しました。[検証記録](SkinningVerification.md#root-motion切り替えの追加検証2026-10-15)と[数値結果](SkinningRootMotionVerification.json)を参照してください。

## GPUスキニング動画の更新（2026-10-15）

22:26〜22:37（日本時間）の録画8本へ差し替えました。Direct3D12／VulkanのVS・Compute Cache・Mesh Shaderによる1体のUAL_Walkを6本、両APIのMesh Shaderによる64体のUAL_Runを2本掲載しています。すべて1280×720、Root motion OFF、速度1です。Mesh ShaderはAS／TaskとAnimated frustumを使用しています。差し替え後のVulkan Compute・Meshの1体動画も前半・中間・後半で実行画面を確認しました。

README、API別6記事、GPU共通章、ローダー、アセット、発展編目次、検証記録の参照と説明を更新しました。8本のMP4は再圧縮せず、SHA-256と容量を照合し、中間フレームから1280×720のJPEGプレビューを作成しました。動画・画像・記事のローカルリンクとManifestの対応を確認しています。[動画一覧](Assets/Videos/README.md)と[元ファイル・設定・SHA-256](Assets/Videos/Manifest.json)を参照してください。今回の変更はドキュメントと動画のみで、既存のビルド・GPU検証・性能測定結果は保持しています。

## 基本24：Dual Quaternion Skinning（2026-10-15）

両APIに独立した24_DualQuaternionSkinningを追加しました。既存アセットをそのまま使い、Indexed VSのLBS／DQSをImGuiで切り替え、Shadow／PBRへ同じ変形を適用します。初期値はDQS、Root motionはOFF。実行時の150度手首ねじり確認、A／Tポーズ、既存6クリップ、1／64体を選べます。配布glTF／GLB／Blenderと素材のライセンスは変更していません。

全68アプリ＋2 CPUライブラリのRestore・x64 Debug／Releaseビルド、SPIR-V、vcxproj／filters照合、全68アプリの両構成ライフサイクル、旧ローダー・既存スキニング・09 ImGui回帰、新2アプリのカメラ／Shader欠落／実UI入力を確認しました。正常試験のDebug検証エラー0件。427姿勢の剛体検査と独立NumPy DQS照合、LBSと基本22のDepth／Shadow／HDR完全一致も通過しています。

[共通DQS解説](DualQuaternionSkinning.md)、[API別記事](Direct3D12/24_DualQuaternionSkinning.md)、[検証・Release 1／64体のLBS／DQS実測](DualQuaternionVerification.md)を追加しました。画像はすべて1280×720の実GPU出力です。

## DQS Compute／Mesh Shaderへの展開（2026-10-16）

両APIに基本25_DualQuaternionComputeCacheとAdvanced11_DualQuaternionMeshSkinningを追加しました。Computeは1フレーム1回の変形結果をShadow／Colorへ共有し、Meshは各MSで直接変形します。両者ともLBS／DQS切替と共通の再生操作を継承し、初期値はDQS／Root motion OFFです。配布アセット・モーション・ライセンスは保持しています。

MeshはDQS正規化を考慮した球をCPUで更新し、カメラ／ライトを独立に判定します。LBSでは従来のAABBへ切り替えます。90条件・約333万Meshlet頂点のCPU境界検査、各構成約119万件のGPU球との照合で境界外0件。両API・両構成の92数値ケースと104描画比較が通過し、Depth／Shadowは完全一致でした。HDRの少数の差とVulkan Debug MSの複合型Load対策は[検証記録](DualQuaternionStagesVerification.md)へ明記しています。

全72 APIアプリ＋2 CPUライブラリをRestore・x64 Debug／Releaseでビルドし、SPIR-Vとfiltersを検証しました。全72アプリの両構成ライフサイクル、新DQS 6アプリの最終再実行、既存ローダー・スキニングCPU／GPU・09 ImGui、カメラ、Shader欠落も通過。正常Debug試験の検証エラー0件です。

[共通DQS実行方式](DualQuaternionStages.md)、API別4記事と既存2記事、目次、1280×720の実GPU画像、Release 32条件の性能を更新しました。Computeがこのモデルの64体ではVertexより遅い結果や、TaskのCPU境界更新費用も省略せず記録しています。
