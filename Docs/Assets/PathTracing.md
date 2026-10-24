# パストレーシングシリーズの共通アセット

専用Roomは、局所的な拡散間接光と粗い鏡面反射を観察するための自作シーンです。Cornell Box風の色壁を使いますが、測定済みCornell Boxのデータを再現したものではありません。

![実GPUの室内](../Direct3D12/Images/Advanced18_PathTracingBasic-room-256.png)

約5×5×3.5 mの前面が開いた部屋に、白い箱、粗さ0.12と0.55の金属球、既存ShaderBall、天井の発光面を配置しています。背景は黒で、点光源・太陽光・事前計算IBLはありません。発光メッシュから出た光が室内で反射します。

| データ | 保存先・役割 |
|---|---|
| PathTracing.blend | SourceArt/Blender/PathTracingの編集正本。画像を梱包 |
| PathTracing.gltf／BIN／Textures | Assets/Models/PathTracingの配布形状・材質・画像 |
| PathTracing.json | Assets/Scenes/PathTracingのモデル参照とRoom／RoomCloseカメラ |
| Manifest.json | 配布・正本・スクリプトのSHA-256、流用ShaderBallの出典ハッシュ |

10 Mesh、7 Material、12 Nodeです。Scene、Node、Mesh、Primitive、Materialは`extras.id`で識別します。保存正本からglTFを書き出して使用し、実行時にモデルを生成しません。後続サンプルも同じファイルを参照します。

座標は右手系・Y-up・mです。CPUローダーがZ、三角形の巻き順、接線Wを一度だけ反転して、既存サンプルの左手系へ変換します。Albedo／EmissiveはsRGB、Normal／ORMは線形です。シェーダはORMのG/Bを使い、RのAOは照明へ掛けません。ShaderBallの細かな発光模様も実際のEmissiveとして扱います。

`pathtracing-scene-1`は既存の静的glTFプロファイルを土台にし、太陽光・環境・カメラ経路のないシーンを許可します。`LoadPathTracingScene`が読み込みます。従来の`LoadWorkshop`は引き続き`workshop-scene-1`だけを受け付けます。

Workshopは既存のStandard／Stress、形状・画像・カメラを流用します。Opaque／OpaqueMaskを選べます。描画方式の追加によって元のWorkshopとShaderBallの正本・配布データを編集する必要はありません。

```powershell
.\Tools\TestPathTracingAssets.ps1
.\Tools\TestPathTracingCpu.ps1
```

アセット試験は固定版glTF-Validator、アクセサ・法線・接線・ID・参照・ハッシュ、画像を梱包した正本だけからの隔離再書出しを検証します。編集手順は[正本のREADME](../../SourceArt/Blender/PathTracing/README.md)を参照してください。自作データと流用ShaderBallはMIT License、Copyright (c) 2026 masafumiです。
