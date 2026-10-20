# Vulkan 第28回：操作するスキニングキャラクター

第22回のVertex Shaderスキニングへ、WASD／XInputによる歩行・走行・ジャンプと三人称カメラを組み込みます。[共通解説](../CharacterController.md)のCPU処理をDirect3D12版と共有し、Vulkanのリソース更新と描画・同期はこのプロジェクトで実装します。

[キャラクターコントローラ](../CharacterController.md#キャラクターコントローラとは)が位置・速度・向き・接地状態を更新し、[モーションブレンド](../CharacterController.md#モーションブレンドとは)が動作の切替に中間姿勢を作ります。前者の配置を`ActorWorld`へ、後者の姿勢をボーンパレットへ反映し、GPUのスキニングでロボットを変形します。

![操作中のロボットと床の目印](Images/28_CharacterController-walk.png)

## 実行と要件

`Vulkan_28_CharacterController`をx64 Debug／Releaseで実行します。Vulkan 1.3、Dynamic Rendering、Synchronization2を使用します。変形方式はVertex ShaderのLBSなので、Mesh ShaderやRay Queryは必要ありません。HLSLはインストール済みVulkan SDKのDXCでコンパイルし、SPIR-Vをビルド時に`spirv-val`へ渡します。

Windows上のVulkanアプリなので、ゲームパッドはWindows SDKのXInputを使用できます。WASD／左スティックで移動、Shift／L3保持で走行、Space／Aでジャンプ、右ドラッグ／右スティックでカメラを回転します。R／Viewで配置・視点・動作を初期化します。

## 操作動画

ユーザー提供の[Vulkan版の操作録画](<../Assets/Videos/Shader.jp Vulkan 28_CharacterController 2026-10-21 01-05-58.mp4>)です。1280×720、約24.5秒。元のMP4を再圧縮せずに保存しています。

固定入力による[5秒の自動操作動画](../Assets/Videos/Vulkan_28_CharacterController.mp4)も用意しています。出典とSHA-256は[動画記録](../CharacterControllerVideos.json)を参照してください。

## フレーム更新とバッファの書込み

`OnUpdate`は入力、固定刻みのキャラクター更新、姿勢ブレンド、診断UI、追従カメラを処理します。GPUリソースを所有しないCPU処理です。

`OnRender`では前フレームのFence完了を確認してから、Mapped Bufferへ書き込みます。Camera／Light定数、現在のボーンパレット、各配置のWorldと法線用行列を更新し、同じ内容をShadowとColorで使用します。使用中のBufferを入力イベントから書き換える経路はありません。

```text
入力とシミュレーション
    ↓
Fence完了待機 → Camera／Palette／Instance更新
    ↓
Acquire → Command記録 → Submit → Present
```

Upload BufferとUI BufferにはHost Visibleのメモリを使い、可能ならHost Coherentを選びます。非Coherentのメモリを選んだ場合は書込み後にFlushします。Host CoherentでもGPUが読んでいる最中の上書きが安全になるわけではなく、完了待機の順序が必要です。

## 同じ姿勢からShadowとPBRを描く

`CharacterPanel::UpdateSkin`で、ローカル姿勢から親子のGlobal行列を作り、逆バインドとMeshノードの逆行列を使ってPaletteを生成します。モデルは-Zが前方で、キャラクターの旋回と配置移動は`ActorWorld`に入ります。

```text
MeshLocal = Σ weight[k] * (pBind * Palette[joint[k]])
pWorld    = MeshLocal * MeshGlobal * ActorWorld
pClip     = pWorld * CameraVP または LightVP
```

パレットと配置の生成は各Passの前に1回です。床と格子は静的な頂点として扱います。Shadowの範囲は床とジャンプ中のモデルを含む高さ4 mまでに広げ、操作で原点を離れても影を描けるようにします。

![ジャンプ中の本体と床に落ちる影](Images/28_CharacterController-jump.png)

Shadow画像はDepth AttachmentとShader ReadのLayoutをBarrierで切り替えます。ColorはForward PBRとIBLを使い、HDRからトーンマップした後にImGuiを合成します。リサイズ時には既存の完了待機とSwapchain／サイズ依存画像の再作成を使います。

## HLSLと座標規約

Direct3D12と同じ`ReferenceVertex.hlsl`、LBS、PBR用HLSLを使います。DXCは`-spirv -fspv-target-env=vulkan1.3 -HV 2021 -Zpr`でコンパイルします。既存のDescriptor Set／BindingとPush Constantの規約を維持します。

カメラと行列は左手系・row-major、深度は0〜1です。Vulkan側の画面上下は負の高さのViewportで合わせます。入力の左右やモデルの前方を合わせるために、Vulkan版だけ移動軸やボーンを反転する処理は追加しません。

CPUで同じ入力列を評価すれば、両APIの位置・速度・アニメーション状態も一致します。描画APIの違いは、転送先とCommand／同期で確認できます。

## 検証

```powershell
./Tools/TestCharacterControllerCpu.ps1
./Tools/TestCharacterController.ps1 -Configuration Debug -Api Vulkan
./Tools/TestCharacterController.ps1 -Configuration Release -Api Vulkan -CaptureDocs
./Tools/TestSamples.ps1 -Configuration Debug -Topics 28_CharacterController
```

DebugではValidation Layerと同期検証を有効にします。状態ログ、実GPU画像、ResetのRGB一致、リサイズ、最小化／復帰、終了処理を確認します。[検証記録](../CharacterControllerVerification.md)と[Direct3D12版](../Direct3D12/28_CharacterController.md)も参照してください。
