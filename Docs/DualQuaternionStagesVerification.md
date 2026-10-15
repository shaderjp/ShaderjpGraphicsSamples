# DQS Vertex／Compute／Mesh：検証と性能

2026-10-16。基本24、基本25、発展11の両APIを対象にします。[方式・同期・DQS境界](DualQuaternionStages.md)と[DQSの数学](DualQuaternionSkinning.md)も参照してください。新しいアセットやモーションを追加せず、既存Blender／glTF／GLB／UAL素材を保持しています。

## 再現コマンド

```powershell
.\Tools\BuildSkinning.ps1 -Configuration Debug -Restore
.\Tools\BuildSkinning.ps1 -Configuration Release
.\Tools\ValidateProjects.ps1
.\Tools\TestDualQuaternionCpu.ps1
.\Tools\TestDualQuaternionBounds.ps1
.\Tools\TestDualQuaternionStages.ps1 -Configuration Debug
.\Tools\TestDualQuaternionStages.ps1 -Configuration Release
.\Tools\MeasureDualQuaternionStages.ps1
```

`TestDualQuaternionBounds`はDebugのMeshOptimizerCpuライブラリを使います。GPU試験は既存BlenderのPython／NumPyで独立計算します。実行アプリへPython依存は追加していません。`-Quick`はWalk／Twist、64体、LBS、描画パスを短く確認し、正式結果と別の一時JSONへ保存します。比較済みの大きなBINは順次削除し、PNG・ログ・JSONを残します。

## DQSと境界のCPU検証

基本24の427姿勢、Quaternionの符号・並進・4インフルエンス・剛体検査を継続します。新しい境界試験は実際のmeshoptimizer出力を使い、既存8クリップと実行時Twistの5時刻、LBS／DQSの計90条件で全Meshlet頂点を検査します。

3,332,520件の頂点と球を比較し、境界外の頂点は0件でした。LBS端点の包絡がDQSを包めない±80度のねじり例と、4インフルエンスの追加例も確認しています。実装は最大インフルエンス数に応じて正規化の下限を使います。球の余分な領域も含めて[導出](DualQuaternionStages.md#mesh版のdqs境界)に説明しています。

## GPUの数値・画像・可視判定

[Debug結果](DualQuaternionStagesVerification-Debug.json)と[Release結果](DualQuaternionStagesVerification-Release.json)に各ケースを保存します。

両構成とも数値検査92ケース、描画比較104組が通過しました。最大成分誤差はC++参照`1.36172e-6`、GPU読戻し`1.40642e-6`です。GPU側の球に対して独立参照の頂点を延べ1,185,776件照合し、境界外0件でした。画面外Casterのケースでは、Camera／Lightとも候補585個に対し、Cameraの可視数6、Lightの可視数585でした。

- Computeで実際に描画するキャッシュを、独立NumPy DQS／LBS参照と全12成分で照合。位置・法線・接線・UV・handednessの許容成分誤差は`3e-5`。
- Vertex／Meshでは共有変形関数を検査用CSから読戻す検査と、実際のDepth／Shadow／HDRの描画比較を併用。VS／MS出力を直接取得した検査とは区別。
- 1体は全頂点、64体は各Actorの64頂点ごとを数値比較。Meshletの頂点対応表も出力し、独立参照の変形位置がアップロードした球の内部にあることを照合。
- 全6 UALクリップ、A／Tポーズ、Twist、1／64体、LBS／DQS、Root motion ON、画面外Casterを比較。
- Mesh direct、Task culling OFF／ON、同じMeshプロジェクト内のVertex referenceを比較。Camera／Lightの候補数・可視数を別々に読み、画面外Casterがライト側に残ることを確認。
- 新4アプリの実ImGui Checkbox、Twistボタン、Reset、Root motion Checkbox／Stepを操作。DQSへ戻したときとResetで基準GPU画像へ復帰。

描画比較の許容値はDepth `3e-6`、Shadow `5e-6`、HDR `0.012`。浮動小数点やRaster順序による端の差を考慮し、許容値を超える成分は全成分の0.1%または16成分の大きい方までとします。実際の最大差・超過数はJSONへ記録します。Reset比較は完全一致を要求します。

実際にはDepthとShadowは全104組で完全一致でした。HDRでは64体のVS／MS比較に最大226成分（3,686,400成分中、約0.0062%）の許容値超過があり、最大差は`1.73865`です。LBS／MSでも同種の差が生じています。頂点の数値照合とDepthの一致、Meshlet化に伴う三角形順序の違いから、同深度で重なる面のRaster順序差によるものと推定しています。HDRの完全一致とは扱わず、値と超過数を残しています。UIのDQSへの復帰とResetは完全一致でした。

## ビルドと回帰

環境は[元のスキニング環境](SkinningVerification.md#検証環境)と同じです。VS Community 2026 18.10.0、MSBuild 18.10.1、cl 19.51.36257、v145、Windows SDK 10.0.26100.0。GPUはRTX 4070、driver 610.88。D3D12はAgility 1.619.6／DXC 1.9.2609.5、Vulkan SDK 1.4.341.1／target 1.3、HLSL 2021／SM6.6です。

新4プロジェクトを加えた72 APIアプリ＋2 CPUライブラリを同じソリューションに収録しました。Restore、x64 Debug／Release、Vulkan SPIR-V、全vcxproj／filtersのXML照合が通過し、ビルド警告・エラー、未割当・重複Filterは0件です。DebugはD3D12 Debug LayerとVulkan同期Validationを有効にしました。

全72アプリの両構成で1280×720描画、resize、最小化・restore、capture、終了を確認しました。最終変更後の新DQS 6アプリも再実行しています。既存ローダー、既存スキニングCPU／GPU、09 ImGui、新DQSアプリのカメラ・Shader欠落も通過しました。正常試験のDebug検証エラーは0件です。新しい記事画像は1280×720の実GPU出力です。

Vulkan Debug MSは、通常の`-Zi -Od`でDQSを含むShaderの出力が消える現象を実機で再現しました。MS／TaskMeshに`-fspv-reduce-load-size`を加え、`-Zi -Od`を保持した状態で数値・画像・可視判定・UI試験を通過しています。SDK／DXC／driverの構成と[オプションの説明](Vulkan/Advanced11_DualQuaternionMeshSkinning.md)を記録し、原因をcompiler／driverの一方へ断定していません。

## Releaseの比較条件

`MeasureDualQuaternionStages.ps1`でVertex、Compute、Mesh direct、Mesh TaskをLBS／DQSそれぞれ測定します。両API、1／64体の計32条件です。UAL_Walk、Root motion OFF、Hero／StressHero固定カメラ、Shadow＋PBR、Studio IBL、1280×720、1/60秒刻み、60 warmup＋1200測定フレーム、VSyncなし、UIなし、読戻しなしで揃えます。

CPU姿勢、DQ変換、境界／Instance更新、Compute変形、Shadow、Color、GPU Totalを分けて記録します。Task版の境界はDQS時に正規化球、LBS時に影響頂点AABBです。直接MS／Vertex／Computeでは可視判定用境界を更新しません。Computeのキャッシュ確保は最大64体分で、通常の他方式には頂点キャッシュを使いません。

[全32条件のaverage／median／p95と容量](DualQuaternionStagesPerformance.json)。次の表は中央値、単位msです。CS／Shadow／ColorはDQSの値で、GPU Totalは同じ方式内のLBS／DQSを並べます。

| API | 体数 | 方式 | CS | Shadow | Color | Total LBS | Total DQS |
|---|---:|---|---:|---:|---:|---:|---:|
| Direct3D12 | 1 | Vertex | — | 0.2836 | 0.0799 | 0.3953 | 0.4004 |
| Direct3D12 | 1 | Compute | 0.1772 | 0.0522 | 0.0788 | 0.3471 | 0.3451 |
| Direct3D12 | 1 | MeshDirect | — | 0.3840 | 0.0788 | 0.4974 | 0.5007 |
| Direct3D12 | 1 | MeshTask | — | 0.3604 | 0.0809 | 0.4731 | 0.4792 |
| Direct3D12 | 64 | Vertex | — | 0.6615 | 0.4956 | 1.2186 | 1.1990 |
| Direct3D12 | 64 | Compute | 0.3543 | 0.3717 | 0.4618 | 1.2380 | 1.2308 |
| Direct3D12 | 64 | MeshDirect | — | 0.7066 | 0.5560 | 1.2933 | 1.3036 |
| Direct3D12 | 64 | MeshTask | — | 0.5765 | 0.4731 | 1.0803 | 1.0906 |
| Vulkan | 1 | Vertex | — | 0.2922 | 0.0686 | 0.4154 | 0.4027 |
| Vulkan | 1 | Compute | 0.2503 | 0.0505 | 0.0676 | 0.4081 | 0.4140 |
| Vulkan | 1 | MeshDirect | — | 0.3759 | 0.0635 | 0.4844 | 0.4819 |
| Vulkan | 1 | MeshTask | — | 0.3693 | 0.0655 | 0.4791 | 0.4772 |
| Vulkan | 64 | Vertex | — | 0.6501 | 0.4628 | 1.1450 | 1.1584 |
| Vulkan | 64 | Compute | 0.4320 | 0.3842 | 0.5499 | 1.4159 | 1.4162 |
| Vulkan | 64 | MeshDirect | — | 0.7125 | 0.4946 | 1.2460 | 1.2528 |
| Vulkan | 64 | MeshTask | — | 0.7264 | 0.4321 | 1.2070 | 1.2041 |

64体のDQSのCPU側も併記します。Taskの「境界」はInstance更新とDQS球更新、その他の同列はInstance更新だけです。

| API | 方式 | CPU姿勢 | DQ変換 | 境界／Instance |
|---|---|---:|---:|---:|
| Direct3D12 | Vertex | 0.2775 | 0.0943 | 0.0061 |
| Direct3D12 | Compute | 0.2767 | 0.0944 | 0.0062 |
| Direct3D12 | MeshDirect | 0.2759 | 0.0943 | 0.0061 |
| Direct3D12 | MeshTask | 0.2759 | 0.0944 | 0.7969 |
| Vulkan | Vertex | 0.2799 | 0.0944 | 0.0061 |
| Vulkan | Compute | 0.2777 | 0.0944 | 0.0061 |
| Vulkan | MeshDirect | 0.2748 | 0.0943 | 0.0061 |
| Vulkan | MeshTask | 0.2771 | 0.0947 | 0.7985 |

この測定ではComputeの64体GPU TotalはDirect3D12 1.2308 ms、Vulkan 1.4162 msで、Vertexの1.1990／1.1584 msを上回りました。2パス分の変形を1回へまとめても、約91.22 MiBのキャッシュの書込み・読込み・同期を加えたTotalが減るとは限りません。元形状は全Actorで共有し、多くが1ボーン頂点であることもこの比較条件です。各費用の寄与を断定するにはGPU profilerでの帯域・占有率の分析が必要です。

TaskのGPU TotalはDirect3D12で1.0906 msでしたが、CPUのDQS境界更新を含む費用は約0.80 msです。GPU Totalだけでフレーム全体の優劣を決めず、CPU姿勢・DQ変換・境界更新も合わせて評価してください。VulkanではTask 1.2041 ms、直接MS 1.2528 msでした。

元ジオメトリは既存サンプルと同じUpload／Host Visible Bufferです。CPUとGPUは1フレームずつ完了待ちして再利用します。Async ComputeやGPUローカルへの元形状転送を加えた場合は同じ条件で再測定してください。この1回の測定を他のGPU、アセット、パス数の順位へ一般化しません。
