# 発展11. VulkanでDQSをMesh Shader内で実行する

同じロボットをMS内でDQS変形し、ShadowとPBR Colorを描きます。直接MSとAS／Taskによる可視判定を切り替え、[基本24 Vertex](24_DualQuaternionSkinning.md)と[基本25 Compute Cache](25_DualQuaternionComputeCache.md)を比較対象にします。[共通DQS](../DualQuaternionSkinning.md)、[DQS境界の導出](../DualQuaternionStages.md#mesh版のdqs境界)、[検証・実測](../DualQuaternionStagesVerification.md)を先に参照できます。

![DQS Mesh Shaderの実GPU表示](Images/Advanced11_DualQuaternionMeshSkinning.png)

## APIの初期化とリソース

[Sample.cpp](../../Samples/Vulkan/Advanced11_DualQuaternionMeshSkinning/Sample.cpp)がGPU初期化、Buffer作成、Pipeline、描画、同期、終了処理を担当します。VS 2026／v145／SDK 10.0.26100.0、C++20、SM6.6／HLSL 2021です。VK_EXT_mesh_shaderとmeshShader／taskShaderを検査し、vkCmdDrawMeshTasksEXTを取得します。Descriptor Set LayoutとPoolへbinding42のStorage Bufferを追加します。DescriptorおよびPush constantのstageFlagsにはVERTEX／FRAGMENT／COMPUTEに加えTASK／MESHの両方を含めます。

| 入力 | register／Vulkan binding | 内容 |
|---|---|---|
| ウェイト | t96／38 | JOINTS＋WEIGHTS |
| LBS palette | t97／39 | 行列64 byte |
| DQ palette | t98／40 | float4×2、32 byte |
| 検査用UAV | u7／41 | 通常の描画では使わない |
| 可視判定用球 | t99／42 | Actor×Meshlet、float4 |

元Vertex、Index、Meshlet構造、Materialは共有です。meshoptimizerで最大64頂点／96三角形のMeshletを作り、Actorごとにpaletteと球の先頭を渡します。

DebugのMS／TaskMeshだけは`-fspv-reduce-load-size`を追加しています。SDK 1.4.341.1のDXC e3554182とRTX 4070／610.88の組合せで、通常の`-Zi -Od`ではDQSを含むMSの描画出力が消え、複合型Loadを小さくするこのオプションで復帰することを実機確認しました。`-Zi -Od`、SDK、DXC、Vulkan targetは維持します。Releaseは既存の`-O3`です。このオプションの役割は[DXCの公式定義](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/Support/HLSLOptions.td)にあり、SPIR-Vの変換については[公式ドキュメント](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)を参照できます。原因をDXCまたはdriverの一方に断定するものではありません。

## MSとAS／TaskのHLSL

[MeshletMesh.hlsl](../../Shaders/Skinning/MeshletMesh.hlsl)はgroupごとにMeshletを読み、出力数を設定します。各頂点が`TransformMeshVertex`→`SkinVertexAt`を通り、DQSの符号合わせ・正規化・位置／法線／接線の変形を実行します。Colorはcamera VP、Shadowはlight VPを選びます。Pixel側はPBR／IBLまたはShadow depthです。

`CreateMeshPipeline`がShadowとPBR Colorに対し、直接MSとTask＋MSのGraphics Pipelineを作ります。`BindMesh`は16 byteのPush constantを設定します。直接描画は`vkCmdDrawMeshTasksEXT`でMeshlet数を、Task使用時は32候補ごとのgroup数を指定します。Vertex referenceも同じPipeline Layoutで比較できます。

[MeshletTask.hlsl](../../Shaders/Skinning/MeshletTask.hlsl)は候補32個を`VisibleMeshlet`で判定し、可視IDをgroupshared payloadへ詰め、可視数だけMesh Shaderを起動します。直接MSはこの可視判定を省きます。

## DQSを包む球とライトの可視性

LBS用の影響頂点AABBはDQSへ流用しません。[DualQuaternionSkinningBounds.h](../../Samples/Shared/DualQuaternionSkinningBounds.h)で、バインド球とジョイントで変形した中心から`r + K D`の球を作ります。2インフルエンスは`K=sqrt(2)`、3／4は`K=N`でDQの正規化を考慮します。CPUで全頂点を変形する方法は使いません。LBS選択時は従来の影響頂点AABBへ戻します。

球はメッシュローカル空間にあり、Instance Worldへ変換してFrustumに照合します。CameraとLightのFrame／6平面を別々に使います。カメラ外のCasterをライト側から除外しないため、その影が画面内の床に残ります。静的Normal ConeはDQS／LBSとも無効です。Root motionはWorldとライト範囲へ適用します。

Host Visibleの行列、DQ、球を前フレームのFence完了後に更新し、非coherentならFlushします。球のDescriptorはTask／Meshから読めるStorage Bufferです。同じGraphics Queueで実行し、通常描画にCSキャッシュはありません。

`--capture-gpu`だけは共有変形関数を検査用CSから実行して読戻します。これはMS出力を直接取得したものではありません。実際のMesh描画はVertex／ComputeとのDepth・Shadow・HDR、カリングON／OFF、画面外Casterでも照合します。検査用dispatchはベンチマークで無効です。

## 操作と再現

```powershell
& .\bin\x64\Debug\Vulkan_Advanced11_DualQuaternionMeshSkinning\Vulkan_Advanced11_DualQuaternionMeshSkinning.exe `
  --skinning dqs --characters 64 --render-path task --meshlet-culling frustum
```

初期値はDQS／Walk／AS・Task／Animated frustum／1体／Root motion OFFです。ImGuiでVertex reference／Mesh direct／AS・Task、Culling、Meshlet色を切り替えます。`--render-path vertex|mesh|task`、`--meshlet-culling off|frustum`で同じ条件を固定できます。LBS／DQS、150度ねじり、6クリップとA／Tポーズ、再生・Root motion・1／64体の操作も共通です。

![64体の独立姿勢とShadow](Images/Advanced11_DualQuaternionMeshSkinning-64.png)
