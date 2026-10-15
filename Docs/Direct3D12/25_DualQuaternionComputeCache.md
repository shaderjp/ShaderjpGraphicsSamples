# 25. Direct3D12でDQSをComputeキャッシュする

基本24のDQSをCSで一度だけ変形し、同じ頂点をShadowとPBR Colorで使います。[DQSの数学](../DualQuaternionSkinning.md)、[3方式とキャッシュの理由](../DualQuaternionStages.md)、[数値検証と実測](../DualQuaternionStagesVerification.md)を合わせて参照してください。

![DQS Computeキャッシュの実GPU表示](Images/25_DualQuaternionComputeCache.png)

## このプロジェクトの入力と初期化

[Sample.cpp](../../Samples/Direct3D12/25_DualQuaternionComputeCache/Sample.cpp)にデバイス初期化、Buffer、Pipeline、転送、Barrier、描画、終了処理があります。VS 2026／v145／Windows SDK 10.0.26100.0、C++20、x64 Debug／Releaseを維持します。Agility SDK 1.619.6、DXC 1.9.2609.5、SM6.6／HLSL 2021を使います。Mesh Shader Tierは要求しません。Root SignatureにJOINTS／WEIGHTS、LBS行列、DQ、キャッシュUAVのTableを置きます。

元頂点とインフルエンスは共有SRV、DQ／行列はUpload heap、変形結果はDefault heapのUAV対応Bufferです。CreateSkinResourcesで最大64体分のキャッシュを確保し、UpdateSkinBuffersで現在のpaletteを転送します。

入力はt96／t97／t98／u7です。1ジョイントはLBS行列64 byte、DQ32 byte。ImGui切替のため両方のpaletteを毎フレーム生成します。変形済み頂点は位置＋UV、法線＋UV、接線のfloat4×3＝48 byteです。元形状とIndexは全Actorで共有します。

## HLSL：DQのブレンドをCSへ移す

[SkinningCompute.hlsl](../../Shaders/Skinning/SkinningCompute.hlsl)の64 thread groupが元頂点の連続範囲を処理します。dispatchInstanceからActorの出力先とpalette先頭を取り出します。

```hlsl
MeshInstance instance = meshInstances[dispatchInstance];
if (id.x >= dispatchBase) return;
skinCache[instance.primitive.y + id.x] =
    SkinVertexAt(id.x, instance.primitive.z);
```

`BindMesh`のmode bit `0x10000`がDQSを選びます。`SkinVertexAt`の内部は[共通DQS HLSL](../../Shaders/Skinning/DualQuaternion.hlsli)で、最大ウェイトのQuaternionへReal／Dualの両方の符号を合わせ、正規化し、位置・法線・接線を変形します。LBSに切り替えると同じdispatchで行列ブレンドを実行します。

[CacheVertex.hlsl](../../Shaders/Skinning/CacheVertex.hlsl)は変形を再計算せず、キャッシュ頂点へWorldとlight／camera VPを掛けます。床は静的Vertex Bufferを使います。PBR Pixel Shader、Studio IBL、2048×2048 Shadow、3×3 PCF、HDRは基本24と同じです。

## APIの描画と同期

`PrepareMeshIndirect`でキャッシュを`UNORDERED_ACCESS`へ遷移し、Actorごとに`Dispatch((vertexCount+63)/64,1,1)`します。その後`VERTEX_AND_CONSTANT_BUFFER`へ遷移します。この遷移がUAV書込みと頂点読取りを順序付けます。

`DrawObjects`のShadow／Color両方で、同じResourceとActorのバイトoffsetをVertex Buffer Viewへ設定し、`DrawIndexedInstanced`します。両描画の間にCSは再実行しません。次フレームも同じGraphics Queueを使い、Fence完了待ちの後にpalette・キャッシュ・UIを再利用します。

キャッシュは現在の体数分だけ書きます。1体で約1.43 MiB、64体で約91.22 MiBです。Shadow＋Colorの2パスで変形計算を繰り返す費用を減らす一方、書込み・読込み・同期と確保領域が増えます。パス数と対象頂点数を揃えて[性能表](../DualQuaternionStagesVerification.md)を評価してください。

## 操作と再現

```powershell
& .\bin\x64\Debug\Direct3D12_25_DualQuaternionComputeCache\Direct3D12_25_DualQuaternionComputeCache.exe `
  --skinning dqs --animation UAL_Walk --characters 64 --root-motion off
```

初期値はDQS／Walk／1体／Root motion OFFです。ImGuiでLBS／DQS、6クリップとA／Tポーズ、150度手首ねじり確認、速度・Seek・Pause・Step、1／64体、Shadow、PCFを切り替えます。ResetはDQS／Walk／Root motion OFFへ戻します。Root motionの進行トラックは既存サンプルと共通で、CSのメッシュローカル変形の後にInstance Worldへ適用します。

`--capture-gpu`では描画に使ったキャッシュを読戻し、独立NumPy参照と位置・法線・接線を比較します。Vertex版／Mesh版とはDepth・Shadow・HDRも比較します。性能測定では読戻しを無効にします。

![64体の独立姿勢とShadow](Images/25_DualQuaternionComputeCache-64.png)
