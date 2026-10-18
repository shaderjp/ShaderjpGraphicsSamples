# インライン・レイトレーシングの検証記録

基本26・27、発展16・17の両APIを対象とします。検証日：2026-10-18。[共通解説](InlineRaytracing.md)

## 実際に使用した環境

| 項目 | 値 |
|---|---|
| Visual Studio | Visual Studio 2026 Community、18.10.12201.205 |
| MSVC | x64 19.51.36257、ツールディレクトリ14.51.36231 |
| Windows SDK／言語 | 10.0.26100.0、C++20、HLSL 2021、SM6.6 |
| D3D12 | Agility SDK 1.619.6、DXC 1.9.2609.5（01b62ad47） |
| Vulkan | SDK 1.4.341.1、付属DXC 1.9.0.5180（e35541826）、target Vulkan 1.3 |
| SPIR-V Tools | v2026.1、fbe4f3ad |
| GPU／ドライバ | NVIDIA GeForce RTX 4070／32.0.16.1088 |
| 画面 | 初期1280×720、奇数サイズ641×479 |

## 検証状況

- 88 APIプロジェクト＋2 CPUライブラリのXML・フィルター検証、NuGet Restore、全ソリューションのx64 Debug／Releaseビルドを通過。
- DXIL／SPIR-V、RayQuery宣言、CPU／SPIR-Vの配列Strideと定数Offsetを検査。
- 既存OBJ／glTF／WIC／バイナリローダー試験を通過。
- 専用のGPU機能・画像比較試験はDebug／Release、両APIで通過。正常Debug実行では検証レイヤーを有効にし、検証エラー0件。
- 基本26 Shadow Map方式と基本18はG-bufferがバイト一致し、HDR最大差0。基本27の反射OFFと基本26もHDR最大差0。
- 既知形状で太陽の遮蔽と自己交差、画面外・MASK・片面／両面の裏面・回転／移動Instanceの反射、環境Miss、反射先の太陽の影、点光源より先の遮蔽物を確認。
- 発展16のbound／textures／resources、単一物体のGPU Drivenを既存15と全画素で比較。HDRは各成分で絶対1e-3＋相対2e-3以内。
- 密なGalleryのGPU Drivenでは、近接した面が同じD32深度へ量子化され、可視リストをAtomicで詰める順番により別Instanceの面が選ばれる。通常配置はDepthのバイト一致を要求し、99%以上を占める同じG-buffer入力の画素でHDRを検査する。直接Mesh描画とのDepth差も2e-6以内で確認する。上記の単一物体と通常の3つのバインド方式では全画素を比較する。
- 発展17の全ライト／Tiled、影付き0・8・全灯、容量超過、空タイル、ライト移動、Stress、レイ数診断を確認。641×479と実UI入力／Resetも両構成・両APIで通過。
- 発展17のResetは加算順序によりFP16 HDRの最大差0.0009765625、PNGで最大1階調の差がある。HDRは共通の許容差内、PNGは1階調以内を要求し、基本26／27・発展16はPNGの完全一致を要求する。
- 日本語のAPI別8記事、共通解説、1280×720の実GPU比較画像38枚を追加・目視確認。[数値結果と画像SHA-256](InlineRaytracingValidation.json)。
- 全88本のライフサイクルをDebug／Releaseで通過。1280×720の描画・キャプチャ、640×480へのリサイズ、最小化・1280×720への復帰、終了を確認。正常Debug実行の検証エラー0件。
- 既存Deferred、RT Pipeline、Mesh Shader、Bindless、09 ImGuiの回帰試験をDebug／Release・両APIで通過。Bindlessは各構成22条件で有限なHDRと参照方式間の一致を確認。
- Releaseは8条件×両API×5反復、計80反復・96,000測定フレームを完了。各反復の60準備／1,200測定フレーム、条件、CSVの有限な正値、実行ファイルとシェーダのハッシュを検査。AS初期構築と定常GPU時間、メモリを分けて保存。

| 構成／API | 発展16 texturesと15のHDR最大差 | 全ライト／TiledのHDR最大差 | 密集GPU DrivenのG-buffer不一致画素（921,600画素中） |
|---|---:|---:|---:|
| Debug／D3D12 | 0.000127435 | 0.000976563 | 323 |
| Debug／Vulkan | 0.000131845 | 0.000976563 | 152 |
| Release／D3D12 | 0.000259578 | 0.0078125 | 821 |
| Release／Vulkan | 0.000131845 | 0.000976563 | 1,023 |

Release／D3D12の全ライト／Tiledの最大差は参照値8.2109375の成分に現れ、許容差は0.017421875です。全成分で差／許容差の最大比は約0.4485で、明るいHDRを含め同じ基準を満たしています。

## 再実行

    Tools/ValidateProjects.ps1
    Tools/TestInlineRaytracingShaders.ps1 -Configuration Debug
    Tools/TestInlineRaytracing.ps1 -Configuration Debug -CaptureDocs
    Tools/TestInlineRaytracing.ps1 -Configuration Release -CaptureDocs
    Tools/TestInlineRaytracing.ps1 -Configuration Release -BenchmarkOnly

関連回帰は各構成で次のスクリプトを実行し、画像とログを.cacheへ保存します。

```powershell
foreach ($configuration in 'Debug','Release') {
    Tools/TestSamples.ps1 -Configuration $configuration
    Tools/TestDeferred.ps1 -Configuration $configuration
    Tools/TestRaytracing.ps1 -Configuration $configuration
    Tools/TestMeshShaders.ps1 -Configuration $configuration
    Tools/TestBindless.ps1 -Configuration $configuration
    Tools/TestImGui.ps1 -Configuration $configuration
}
```

TestInlineRaytracingは既存のGPUベンチマークを検出すると待機します。性能測定中に別のサンプルが動いた反復は採用せず再測定します。実行ログ、浮動小数点画像、ベンチマークJSON／CSVは.cache/inline-testsに保存します。

<!-- INLINE_PERFORMANCE_BEGIN -->
## Releaseの実測値

1280×720、60フレーム準備後の固定経路1,200フレームを5反復。表は各条件6,000フレームを統合したGPU totalのmsです。UI・VSyncを無効にし、他サンプルと重なった反復は再測定します。

| API | 条件 | 平均ms | 中央値ms | p95 ms | 反復平均の最小〜最大ms | AS初期構築ms |
|---|---|---:|---:|---:|---:|---:|
| Direct3D12 | 基本26：RayQueryの太陽の影 | 0.797 | 0.499 | 0.737 | 0.747〜0.862 | 148.55 |
| Direct3D12 | 基本26：Shadow Map | 1.140 | 0.678 | 3.499 | 1.050〜1.216 | 157.43 |
| Direct3D12 | 基本27：太陽の影＋反射 | 1.422 | 0.842 | 8.472 | 1.223〜1.501 | 151.36 |
| Direct3D12 | 発展16：Inline Hybrid | 10.870 | 12.903 | 16.637 | 9.388〜11.643 | 167.14 |
| Direct3D12 | 発展15：RT Pipeline比較 | 10.215 | 12.505 | 15.756 | 9.447〜11.355 | 171.05 |
| Direct3D12 | 発展17：Tiled・影付き8灯 | 2.101 | 1.198 | 9.619 | 1.951〜2.230 | 150.50 |
| Direct3D12 | 発展17：全ライト評価・影付き8灯 | 4.369 | 2.578 | 11.487 | 4.076〜4.737 | 152.14 |
| Direct3D12 | 発展17：Tiled・影付き32灯 | 2.377 | 1.438 | 9.937 | 2.120〜2.536 | 156.42 |
| Vulkan | 基本26：RayQueryの太陽の影 | 1.299 | 0.758 | 6.566 | 0.956〜1.606 | 69.54 |
| Vulkan | 基本26：Shadow Map | 1.558 | 0.844 | 8.753 | 1.407〜1.806 | 68.56 |
| Vulkan | 基本27：太陽の影＋反射 | 1.721 | 0.995 | 9.276 | 1.515〜1.838 | 74.96 |
| Vulkan | 発展16：Inline Hybrid | 6.764 | 5.332 | 15.362 | 5.223〜11.856 | 50.52 |
| Vulkan | 発展15：RT Pipeline比較 | 7.131 | 5.518 | 16.725 | 5.259〜12.809 | 45.84 |
| Vulkan | 発展17：Tiled・影付き8灯 | 11.170 | 8.616 | 39.450 | 8.065〜13.427 | 413.32 |
| Vulkan | 発展17：全ライト評価・影付き8灯 | 55.997 | 52.258 | 127.752 | 38.017〜68.581 | 695.22 |
| Vulkan | 発展17：Tiled・影付き32灯 | 24.171 | 23.381 | 57.407 | 10.175〜28.670 | 907.07 |

AS初期構築はCPUでのデータ準備・GPU構築完了待ちを含む起動時の壁時計時間です。基本26の両方式はASを保持します。発展16／15の既存メモリ報告と基本26／27・発展17の追加Ray Scene Buffer報告は範囲が異なるため、APIや系列をまたいで合計メモリとして比較しません。

影付き8灯と32灯は画の条件が変わる負荷比較です。全ライト／Tiledでは同じ8灯を影対象とします。発展15／16はStandard・512配置・64材質・textures・GPU Driven OFFを固定します。

反復平均の幅は特にVulkanで大きく、発展15／16の平均差は反復間の変動より小さいため、この結果だけで実行方式の優劣を確定しません。発展17の全ライト8灯とTiled8灯では照明の探索・評価量が変わり、Tiled32灯では影付き灯数が変わります。

| API | 系列 | AS記録値 MiB | ASの範囲 | 追加Ray Scene Buffer MiB |
|---|---|---:|---|---:|
| Direct3D12 | 基本26／27・発展17 | 9.500 | AS backing allocation | 4.564 |
| Direct3D12 | 発展16／15 | 9.053 | 既存レポートのAS結果サイズ | 個別のscene buffers記録を参照 |
| Vulkan | 基本26／27・発展17 | 6.687 | AS backing allocation | 4.564 |
| Vulkan | 発展16／15 | 9.053 | 既存レポートのAS結果サイズ | 個別のscene buffers記録を参照 |

[全測定値・環境・入力レポートSHA-256](InlineRaytracingPerformance.json)。GPUやシーンによる差を含み、RayQueryが常に高速になることを保証しません。
[元のJSON・1,200行のCSV・環境](Measurements/InlineRaytracing)も保存しています。
<!-- INLINE_PERFORMANCE_END -->
