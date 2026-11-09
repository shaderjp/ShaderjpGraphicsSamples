# 伝統的な可視性判定：基本31〜34

通常のVertex／Pixel ShaderとIndexed Drawを使い、オブジェクトの境界から描画を省略するシリーズです。フラスタムカリングは**視錐台の外**、オクルージョンカリングは**遮蔽物の陰**を対象にします。Depth Pre-passやEarly-Zによるピクセルの棄却とも区別します。

| 回 | 判定する場所 | 描画を省く場所 | 前提 |
|---|---|---|---|
| 31 FrustumCulling | CPU：AABBと6平面 | CPU：Drawを発行しない | 通常のIndexed Draw |
| 32 OcclusionQueries | GPU：境界のラスタライズとDepth Test | GPU：条件付きDraw | D3D12 Query／Predication、Vulkan Conditional Rendering |
| 33 SoftwareOcclusion | CPU：低解像度Depthと投影矩形 | CPU：Drawを発行しない | CPUソフトウェアラスタライザー |
| 34 HiZOcclusion | Compute：深度ピラミッドと投影矩形 | GPU：条件付きDraw | Compute、Vulkan Conditional Rendering |

各回にDirect3D12／Vulkanの独立プロジェクトがあります。基本的な描画を学んだ後に31〜34へ進み、その後で発展05 GPU Driven Rendering、発展06 Meshlet Cullingを読む順序を推奨します。

発展05はGPUで可視Meshletリストと間接引数を生成します。本シリーズの32・34はCPUが各Drawを記録し、GPUが実行可否を決めます。**GPUで省略した対象数と、CPUが記録したDraw数は一致しません。**

## 共通シーンと比較の単位

[CullingLab](Assets/CullingLab.md)は、曲がり角、壁、開口部、柱、動く扉とWorkshopの小物を持つシーンです。Mini／Standard／Stressの検査対象は64／512／4,096配置。建築物は別に数えます。

ローダーのPrimitive配置を一つのカリング単位とし、形状・材質を共有します。すべての方式で同じカメラ、World変換、材質、PBR／IBL、露出を使います。遮蔽物には指定した不透明な実形状を使い、穴のある格子は遮蔽物へ加えません。MASK対象のAlpha Testは色描画で行います。

既定解像度は1920×1080。`--width`／`--height`の指定は初期値の後に適用します。最小化から戻ると、その起動時のサイズへ復帰します。

## 31：AABBと視錐台

行ベクトル・左手系・Depth 0..1のViewProjection行列から、左右・上下・Near／Farの6平面を取り出します。World AABBの中心を **c**、半寸法を **e**、正規化した平面を (**n**, d) とすると、

```text
distance = dot(n, c) + d
radius   = dot(abs(n), e)
distance + radius < 0 なら、その平面の外
```

一つでも完全に外側の平面があれば除外します。境界に接するものは残し、全平面の内側と境界交差を区別して表示します。World AABBは現在の変換でローカル境界の8隅を変換して作ります。

Freeze culling cameraは判定カメラを固定したまま観察カメラを移動する診断です。黄色の視錐台と各AABBを表示します。この状態では観察カメラの正解画像とは一致しないため、通常の画像比較には使いません。

## 32：Queryと条件付きDraw

```text
CPU Frustum
  → 不透明な遮蔽物のDepth
  → 検査対象のAABBを色・Depth書込みなしで描画
  → Query結果をバッファへ解決
  → 通常のIndexed Drawを条件付き実行
```

AABBのDepth Testを通るサンプルが一つでもあれば可視候補です。AABBは実形状より大きいので、実際には隠れていても残る場合があります。これは許容します。ゼロの場合だけ省略します。

QueryはCPU Frustumを通った対象へまとめて発行し、連続したQuery領域をまとめて解決します。判定結果をCPUへ読み戻してから色描画を発行する待合せはありません。一方で、GPU上のQuery／Resolve／後続Drawの依存には費用があります。UIや検証用の読戻しは描画後の完了待ちを経て行います。

D3D12のPredicationは64-bit、VulkanのConditional Renderingは32-bit値を参照します。Vulkan版では64-bit Query結果の上下32-bitを調べ、0／1へ正規化してから渡します。Queryの詳細と条件付きDrawは各APIのC++で追えます。[Microsoft Predication](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication)、[Khronos Conditional Rendering](https://docs.vulkan.org/samples/latest/samples/extensions/conditional_rendering/README.html)

Depthのみモードは同じ遮蔽物Pre-passを実行し、遮蔽判定によるDraw省略を行いません。Early-ZでPixel Shader処理を減らしても、Draw発行や頂点処理まで自動的に消えるわけではない点を比較します。

## 33：CPUソフトウェアDepth

既定は描画解像度の縦横1/4。1/2と1/8も選べます。CPUは遮蔽物の三角形を同次座標でクリップし、低解像度のDepthへラスタライズします。

単に低解像度セルの中心が三角形内にあるだけでは、実際には見えている隙間を塞ぐ危険があります。この実装は**セル全体が一つの三角形で覆われると確認した場合だけ**書き込みます。Depthも三角形の最遠値を使って遠方へ寄せます。三角形の境目には未被覆セルが残るため、除外率は下がりますが誤った遮蔽を防ぐ方向です。

面の向きもGPUと一致させます。片面材質の裏面までCPU Depthへ書くと、カメラが壁の内側やNear面付近に来たとき、GPUでは見えている対象を誤って隠せます。両面材質だけは両方の向きを受け入れます。このケースは実GPUとの回帰比較に含めています。

対象AABBの8隅を投影し、外側へ広げた画面矩形を作ります。その矩形に重なるすべてのセルより対象の最近Depthが奥の場合に限り除外します。単一スレッドの参照実装であり、SIMD・タイル化・複数スレッドの最適化ライブラリとの性能同等性を目的にしていません。

CPU DepthはCPU判定に使い、GPUへのアップロードはDepth診断表示時だけ行います。通常描画の可視性判定にはGPU読戻しを使いません。

## 34：ComputeによるHi-Z

Near＝0／Far＝1なので、領域全体を隠せると確認するには、その領域の**最大Depth**が必要です。未描画のDepth＝1が一つでも入れば、その領域を強く遮蔽したとは扱いません。

```text
遮蔽物Depth
  → Mip 0へコピーし、2冪の余白を1で埋める
  → 2×2の最大値を順次縮約
  → AABBの投影矩形が最大2×2セルに収まる段を選ぶ
  → 重なる全セルの最大Depthと対象の最近Depthを比較
  → 0／1のPredicateを通常Drawへ渡す
```

ピラミッドは各段を一つのfloatバッファへ順に並べます。各段の寸法とオフセットを明示し、D3D12／Vulkanの双方で同じHLSLを使います。実画面と2冪の領域の座標を混同しないこと、縮約の各Dispatch間に書込み→読取りの依存を置くことが要点です。

画面の奇数サイズも2冪へ拡張し、右端・下端の入力を捨てません。Hi-Z mip表示では選択した段を可視化できます。表示の明度は`pow(1 - depth, 0.25)`であり、距離の線形表示ではありません。

BoundsのInspect objectは対象ごとのMip選択を表示します。白い矩形は、その対象が参照する全セルの実画面座標での被覆範囲です。画面矩形より大きいセルを使うほど、空のDepthや開口部が混ざりやすくなります。

Hi-Z画像を調べるときはHi-Zモードを選びます。遮蔽判定を無効にしたモードではピラミッドを構築せず、Depth表示は色描画後のGPU Depthへ切り替わります。CPU DepthはSoftwareモードで生成します。UIの統計とBoundsの色分けは、直前に完了したフレームの診断結果です。

## 誤った除外を防ぐ規則

- Near面をまたぐAABB、カメラを含む境界、投影を確定できないケースは可視候補へ残す。
- 遮蔽物を実際より大きくしない。開口部を塞ぐ代理形状やMASK材質を遮蔽物にしない。
- 同じフレームのカメラ・扉位置・Depthを使う。前フレームのDepthによる時間的な推測は行わない。
- 可視性を決める前に、CPU／GPUが参照するバッファの内容と状態を更新する。GPU完了前に動的定数・UIバッファを上書きしない。

完全に隠れた対象を残すことは描画量の増加です。見えている対象を落とすことは画面の欠落です。検証では後者をゼロにします。

## 操作とCLI

WASD・Q/E・右ドラッグ・Shift・Rのカメラ操作とImGui入力の抑制を継承します。Scene、固定Camera、Camera path、Door X、Resetを共通UIで操作できます。

| オプション | 内容 |
|---|---|
| `--culling` | `off`／`frustum`、32の`depth-only`／`query`、33の`software`、34の`depth-only`／`hiz` |
| `--culling-view` | `final`／`bounds`／`depth`／`ids`。`hiz`は34専用 |
| `--culling-scene` | `Mini`／`Standard`／`Stress` |
| `--culling-door` | 扉のX移動、0〜2.5 m |
| `--culling-object-x` | 最初の検査対象のX移動、−4〜4 m |
| `--culling-divisor` | CPU Depthの縮小係数、2／4／8 |
| `--culling-mip` | Hi-Z表示段 |
| `--culling-freeze` | 31の判定カメラを固定 |
| `--camera` | `Hero`／`Occlusion`／`Opening`／`Near`／`Plaza` |
| `--camera-path --path-frame N` | 0〜1200の固定経路位置 |
| `--capture-gpu --screenshot FILE` | PNG、判定JSON、HDR／Depth、使用中のCPU Depth／Hi-Zを保存 |
| `--benchmark --timings FILE` | 60フレームのウォームアップ＋1,200フレームの計測 |

カリングOFFと比較するときは、Scene、カメラ、時刻、扉、解像度を揃えます。比較に使用するObject IDはHDRのR/Gに8-bitずつ符号化してあり、背景は0です。

## 検証と測定

`Tools/TestCullingCpu.ps1`は6平面、境界交差、投影、Nearクリップ、CPU Depthの被覆、アセット読込を検査します。`Tools/TestCulling.ps1`は実GPUでOFFと各方式を比較し、見えるIDを除外していないこと、ID画素とDepthが一致すること、完成HDR画像が一致することを確認します。Hi-Zの全段はCPU最大値縮約とも比較します。

`Tools/TestCulling.ps1 -UiOnly`は実ImGui入力による扉・OFF・ResetとCLIの再現性、`-MotionOnly`は同一プロセス内の移動・急な振り返り・Reset、`-ResizeOnly`は奇数寸法への変更と復帰画像を検査します。`Tools/TestSamples.ps1`はリサイズ・最小化復帰・キャプチャ・終了を検査します。Debugでは両APIの検証レイヤー、Vulkanでは同期検証を有効にします。

性能はCPU判定、CPU記録、Depth、Query解決、Hi-Z構築、Hi-Z判定、色描画、GPU全体を分けて記録します。遮蔽の少ない視点では判定費用が増えるだけの場合があります。除外率やDraw数だけを高速化の証拠にせず、同条件の総時間を比較してください。

`Tools/MeasureCulling.ps1`はReleaseで各条件を5反復します。Heroは入室する固定経路、`--camera Plaza --benchmark`は上空の円弧経路です。`renderWall`はOnRender全体の実時間で、完了待ち・Present・診断読戻しも含みます。

OFFでも診断比較用のWorld AABBと投影を更新します。CPUの共通準備費用は残ります。また、教材は毎フレームGPUの完了を待つため、複数フレームを並行させたレンダラーのスループットとは区別してください。各方式は17のPBR／IBL・Postprocessを土台にしており、色描画はForward、ShadowとBloomは無効です。

[実機検証記録](CullingVerification.md)に、実行した条件と測定結果を掲載します。
