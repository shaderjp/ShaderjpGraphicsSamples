# GPUスキニング：検証と性能の記録

2026-10-15、日本時間。実機での検証後、ユーザーによる動作確認を経てGitHubへ反映する内容です。[ローダー記事](GltfSkinningLoading.md)、[GPU共通章](GpuSkinning.md)、[Blender制作](Assets/SilverRobotAnimated.md)を参照してください。

## 検証環境

| 項目 | 実測した構成 |
|---|---|
| OS | Windows 11 Pro 10.0.26300 |
| Visual Studio | Community 2026 18.10.0、MSBuild 18.10.1-1.26427.6 |
| C++ | v145、C++20、cl 19.51.36257、tool directory 14.51.36231 |
| Windows SDK | 10.0.26100.0 |
| GPU／driver | NVIDIA GeForce RTX 4070／610.88（Windows 32.0.16.1088） |
| D3D12 | Agility 1.619.6、SDKVersion 619、DXC 1.9.2609.5／01b62ad47 |
| Vulkan | SDK 1.4.341.1、API target 1.3、DXC 1.9.0.5180／e35541826（DLL表示1.10） |
| SPIR-V検証 | SPIRV-Tools v2026.1、target-env vulkan1.3 |
| HLSL | SM6.6、HLSL 2021、row-major、LH、depth 0..1 |
| Blender | 5.2.1 LTS／build 9e2066aef7ef |
| glTF Validator | Khronos 2.0.0-dev.3.10 |
| 画像 | 実GPU／1280×720、Shadow 2048×2048 |

## ビルドと回帰

- RestorePackagesConfig=trueでソリューションのNuGetを復元。
- ソリューション全体のx64 Debug／Releaseビルド成功。新6プロジェクトもv145／SDK設定を維持。最終C++／HLSLビルドの警告・エラー0件。
- 全66 APIアプリと2つのCPUライブラリのvcxproj／filtersをXML照合し、欠落・余分・重複・未宣言Filter・GUID重複0件。
- VulkanのオフラインSPIR-Vをビルド時に検証。さらに新6アプリのDebug／ReleaseのDXIL／SPIR-V計252出力をdxv／spirv-valで検証。
- `Tools/TestLoaders.ps1`の既存OBJ／WIC／BIN／glTF／PBR回帰成功。
- `Tools/TestSamples.ps1`でDebug／Releaseの全66アプリの描画、640×480 resize、最小化・1280×720 restore、capture、shutdown成功。
- DebugのD3D12 Debug Layer／Vulkan Validation Layerを有効化。Vulkanは同期検証も有効。正常試験のValidation errorsはすべて0件。
- 既存09の`TestImGui`を両構成で実行し、実slider入力でGPU画像が変わり、Resetで復帰。
- 新6アプリのカメラ入力を両構成で実行。W移動、右ドラッグで変化、Rで元の画像へ完全復帰。
- 新6アプリのShaderを一時的に欠落させる異常系を両構成で試験。終了コード1、ファイルを含むERROR診断、終了処理のValidation errors 0。Shaderはfinallyで復元。

## アセットとCPU評価

保存済みAnimated blendからglTF＋BIN／GLBを再出力し、配布ファイルとバイト単位で一致しました。Aポーズ原本とAnimated正本のSHA-256は検証前後で変わりません。Khronosの両形式はerrors／warnings／infos／hintsすべて0。[ハッシュと検証結果](Assets/SilverRobotAnimatedAssets.json)

全8 ActionについてBlender評価済みメッシュと独立したNumPy glTFスキニングを照合しました。A/Tは全キー、6クリップは始点・四分点・中央・四分の三・終点、各31,138頂点を比較しています。最大位置誤差は **1.01177987e-06 m**。[Blender照合JSON](Assets/SilverRobotAnimatedValidation.json)

`TestSkinningCpu`はレストpalette、全Action、既知の親子TRS、STEP、Quaternion符号と最短経路、U8/U16 jointsとnormalized weightsを試験します。循環、Skin参照、JOINTS値、負／ゼロ総和／NaNウェイト、欠落／短い／過大BIN、URI逸脱、Accessor範囲、CUBICSPLINE、WEIGHTS_1を拒否することも確認します。

## GPUの数値比較と操作

`TestSkinning`は両構成・両APIで6クリップの1体をVS／CS／MS direct／Taskで描き、64体の独立姿勢も比較します。時刻0.3秒を固定し、Depthはfloat32、Shadow depthはfloat32、線形HDRはfloat16で読み戻します。Computeは**実際にShadowとColorへbindしたVertex Buffer**の位置／法線／接線を読み戻します。1体は全頂点、64体はActorごとに64頂点おきの487頂点を比較します。

| 構成 | C++ vs 独立glTF位置（m） | CS vs CPU全成分 |
|---|---:|---:|
| Debug | 1.18557428e-06 | 3.57627869e-07 |
| Release | 1.18557428e-06 | 3.57627869e-07 |

照合閾値は位置／法線／接線3e-5、Depth／Shadow 3e-6、HDR成分0.012です。画像は輪郭・depth tieのRaster順序差を考慮し、閾値超過の成分を最大0.1%まで許容します。頂点の誤差判定にはこの画像の許容割合を適用しません。詳細は[Debug比較JSON](SkinningGpuDebug.json)、[Release比較JSON](SkinningGpuRelease.json)、[最大値の集計](SkinningVerificationSummary.json)へ保存しています。

| 構成・比較 | Depth最大差 | Shadow最大差 | HDR最大差 | HDR閾値超過の最大成分数 |
|---|---:|---:|---:|---:|
| Debug VS-CS | 0 | 0 | 0 | 0 |
| Debug VS-MS | 0 | 0 | 1.7386475 | 223 |
| Debug animated-culling | 0 | 0 | 0 | 0 |
| Debug stress-culling | 0 | 0 | 0 | 0 |
| Debug offscreen-caster-culling | 0 | 0 | 0 | 0 |
| Release VS-CS | 0 | 0 | 0 | 0 |
| Release VS-MS | 0 | 0 | 1.7386475 | 223 |
| Release animated-culling | 0 | 0 | 0 | 0 |
| Release stress-culling | 0 | 0 | 0 | 0 |
| Release offscreen-caster-culling | 0 | 0 | 0 | 0 |

Shadow OFFが床のHDRを変えること、カメラ外のCasterが落とす影がTaskカリングでも残ることを数値と実画像で確認しました。全クリップでOFF／frustumの画像を比較しています。静的normal coneは無効です。

歩行の別時刻でCompute頂点が変化、durationを超えた歩行が折り返し、JumpStartの終端後100秒／101秒で同じ頂点を保持、速度2が速度1の2倍の時刻進行となることを確認しました。実ImGui sliderへの入力でTimeをSeekし、Reset後のHDRがbaselineへ完全一致しました。1フレーム送りは30 fpsです。JumpStart／Loop／Landは自動連結しない独立選択です。

## ユーザー確認と実行録画

2026-10-15にユーザーが動作を確認した後、22:26〜22:37（日本時間）の最新録画8本へ更新しました。[録画一覧](Assets/Videos/README.md)と[元ファイル名・SHA-256](Assets/Videos/Manifest.json)を保存しています。すべて1280×720で、Direct3D12／VulkanのVS・Compute Cache・Mesh Shaderの1体Walkが各1本、Mesh Shaderの64体Runが両APIで各1本です。全8本ともRoot motion OFF、速度1、Loop・Shadow・3×3 PCF・IBLはONです。Mesh Shaderの4本はAS／TaskとAnimated frustumを有効にしています。

録画の前半・中間・後半を確認し、UIの方式・クリップ・体数に合わせて記事へ配置しました。MP4のバイト列を保持し、中間フレームからJPEGプレビューを作りました。性能値は動画のUI表示から採らず、以下の固定条件の実測を使います。

## Root motion切り替えの追加検証（2026-10-15）

6サンプル共通のImGuiへRoot motionチェックボックスを追加しました。初期値とResetはOFFです。現在の配布モデルは水平Root移動を持たないため、Walk／Runの支持足の動きから一定速度の進行トラックを復元しています。モデル・Blender・元ライブラリは変更していません。[復元と操作の説明](GpuSkinning.md#root-motionを切り替える)

x64 Debug／Releaseのソリューション全体を再ビルドし、コンパイラ警告・エラー0件、全プロジェクトのXML／filters整合を確認しました。CPU試験では既知の2 m/sの足運びから2 m/sを復元するケース、Walk／Runの連続移動、その他のクリップの移動量0を検査しています。旧ローダー回帰と09のImGui回帰も両APIで成功しています。

GPU試験は両構成・全6アプリで実CheckboxをON→1フレーム送り→OFF→Resetと操作しました。ON／OFFで同じポーズ時刻を保ったまま、実Depth画像のキャラクターが移動することを確認し、Reset後のHDRは初期状態と完全一致しました。更新前のReleaseと、Root motion OFFの全6アプリのHDRもビット単位で一致しています。

Root motion ONでVS／Compute／MeshのDepth・Shadow・HDRを比較し、64体のカリングOFF／frustumも照合しました。周期をまたいでも前進位置が巻き戻らず、速度2で移動時間も2倍、速度0とPauseで停止することをGPU読戻しで確認しています。リサイズ・最小化復帰・キャプチャ・終了も両構成で通過し、DebugのValidationエラーは0件です。更新した記事画像も1280×720です。

具体的な状態、Depthが変化した画素数、Reset・旧表示との一致、移動ONの画像比較は[Root motion検証JSON](SkinningRootMotionVerification.json)へ保存しています。

## Release性能

以下の性能表は、Root motion切り替えを追加する前の初回検証結果です。水平移動は現在のRoot motion OFFに相当します。上の実行録画は切り替え追加後の最新版で、性能表とは別に更新しています。

`Tools/MeasureSkinning.ps1`で同一条件を計測しました。1280×720、UAL_Walk、速度1、1体または8×8の64体、Studio IBL、方向光、2048 Shadow、3×3 PCF、UI非表示、Present sync interval 0。1体はHero、64体はStressHeroの固定カメラを全方式で使用します。アニメーション時刻はframe/60で評価し、60 warmup＋1200測定フレームです。Actorは1/64周期ずつ位相をずらします。

GPUはTimestamp、CPUはsteady_clockです。表は1200フレームの平均ms。median／p95と各パスは[実測JSON](SkinningPerformance.json)、各フレームCSVは`.cache/skinning/performance`にあります。GPU TotalはCS変形・Shadow・Color・Sky・HDRポスト処理を含み、CPU評価・Fence待ち・Present待ち・検証読戻しを含みません。既存のHDRポスト処理を再利用しているため、Bloom合成をOFFにしても共通のExtract／Blur作業領域の生成はTotalへ含まれます。

| API | 方法 | 体数 | CPU姿勢 | CPU境界 | CS変形 | Shadow | Color | GPU Total |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Direct3D12 | Vertex | 1 | 0.0062 | 0.0001 | — | 0.2557 | 0.0828 | 0.3791 |
| Direct3D12 | Compute | 1 | 0.0067 | 0.0001 | 0.2069 | 0.0506 | 0.0791 | 0.3740 |
| Direct3D12 | MeshDirect | 1 | 0.0070 | 0.0001 | — | 0.3925 | 0.0916 | 0.5218 |
| Direct3D12 | MeshTask | 1 | 0.0066 | 0.0090 | — | 0.3533 | 0.0868 | 0.4774 |
| Direct3D12 | Vertex | 64 | 0.2779 | 0.0053 | — | 0.7359 | 0.5135 | 1.2983 |
| Direct3D12 | Compute | 64 | 0.2849 | 0.0053 | 0.4091 | 0.3779 | 0.4812 | 1.3174 |
| Direct3D12 | MeshDirect | 64 | 0.2805 | 0.0053 | — | 0.7306 | 0.5574 | 1.3360 |
| Direct3D12 | MeshTask | 64 | 0.2845 | 0.5500 | — | 0.6043 | 0.4603 | 1.1056 |
| Vulkan | Vertex | 1 | 0.0067 | 0.0001 | — | 0.3005 | 0.0696 | 0.4197 |
| Vulkan | Compute | 1 | 0.0067 | 0.0001 | 0.1730 | 0.0513 | 0.0688 | 0.3357 |
| Vulkan | MeshDirect | 1 | 0.0074 | 0.0001 | — | 0.3646 | 0.0772 | 0.4841 |
| Vulkan | MeshTask | 1 | 0.0068 | 0.0094 | — | 0.3701 | 0.0748 | 0.4870 |
| Vulkan | Vertex | 64 | 0.2853 | 0.0053 | — | 0.7008 | 0.4915 | 1.2383 |
| Vulkan | Compute | 64 | 0.2904 | 0.0056 | 0.3746 | 0.3978 | 0.5643 | 1.3839 |
| Vulkan | MeshDirect | 64 | 0.2816 | 0.0053 | — | 0.7147 | 0.5089 | 1.2711 |
| Vulkan | MeshTask | 64 | 0.2837 | 0.5474 | — | 0.7455 | 0.4437 | 1.2352 |

Vertex／Compute／MeshDirectの「CPU境界」はInstance更新を含む小さい計測区間です。MeshTaskだけが骨別AABBの8隅から変形後の保守境界を生成しています。MeshTaskはGPU側の候補を減らす一方で、このCPU費用が増えます。CPUとGPUの時間を単純に足した値をフレーム時間とみなすことも避けてください。

キャッシュの有効領域は1体1,494,624 byte（1.43 MiB）、64体95,655,936 byte（91.22 MiB）。ComputeはUI切替のため64体分を先にGPUメモリへ確保し、1体時は先頭だけを使います。VS／MSは変形キャッシュを持たず、Descriptor用の48 byte dummy Bufferだけです。paletteは64体＋床の53×64＋1行列を先に確保しています。

この結果はShadow＋Colorの2パスと今回のモデルに対するものです。小さいモデルでは変形再利用の利益よりdispatchと同期が目立ちます。64体ではGPUの違いだけでなくCPU境界評価も比較してください。プロダクションの複数フレーム並列化、Async Compute、別形状・多数Shadow cascadeでの性能を保証する計測ではありません。

## 再現コマンドとローカル確認

```powershell
.\Tools\ValidateProjects.ps1
.\Tools\BuildSkinning.ps1 -Configuration Debug -Restore
.\Tools\BuildSkinning.ps1 -Configuration Release
.\Tools\TestSilverRobotAssets.ps1
.\Tools\TestSilverRobotAnimated.ps1
.\Tools\TestLoaders.ps1
.\Tools\TestSkinningCpu.ps1
.\Tools\TestSkinning.ps1 -Configuration Debug
.\Tools\TestSkinning.ps1 -Configuration Release
.\Tools\TestSkinningFailures.ps1
.\Tools\TestSkinningFailures.ps1 -Configuration Release
.\Tools\TestSamples.ps1 -Configuration Debug
.\Tools\TestSamples.ps1 -Configuration Release
.\Tools\TestImGui.ps1 -Configuration Debug
.\Tools\TestImGui.ps1 -Configuration Release
.\Tools\MeasureSkinning.ps1
.\Tools\TestSamples.ps1 -Configuration Release -CaptureDocs `
  -Topics 22_VertexSkinning,23_ComputeSkinningCache,Advanced10_MeshShaderSkinning
```

ソリューションで新規6プロジェクトを起動できます。Blenderでは[SilverRobotAnimated.blend](../SourceArt/Blender/SilverRobot/SilverRobotAnimated.blend)を開いてActionを選択してください。検証用のモデルとBIN、ライセンスはexeの隣へビルド時にコピーされます。
