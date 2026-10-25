# BSDF比較用アセット：既存Roomとガラスの追加モデル

発展19は[発展18のRoom](PathTracing.md)の壁・床・天井面光源とShaderBallを再利用します。既存の正本とglTFは変更しません。[共通解説](../BRDFAndBSDF.md)／[Direct3D12](../Direct3D12/Advanced19_BSDFMaterials.md)／[Vulkan](../Vulkan/Advanced19_BSDFMaterials.md)

![比較シーン](../Direct3D12/Images/Advanced19_BSDFMaterials-full.png)

## 追加するもの

| 形状 | 目的 | 初期マテリアル |
|---|---|---|
| 透明球 | 背景の屈折、Fresnel反射 | IOR 1.5、粗さ0、吸収なし |
| すりガラス球 | 粗い反射と透過 | IOR 1.5、粗さ0.3、吸収なし |
| くさび形ガラス | 形状と通過距離による色 | IOR 1.5、粗さ0、青緑の吸収 |
| 0.1 m／0.5 mの板 | 同じ吸収係数での厚み比較 | くさび形と同じ吸収マテリアル |
| 格子背景 | 屈折した像とぼけの観察 | 不透明Lambert＋GGX |

ガラスのBase Colorは白、Metallicは0、Transmissionは1、alphaModeはOPAQUEです。色はVolumeの`attenuationColor = (0.18, 0.65, 0.85)`、`attenuationDistance = 0.5 m`で与えます。透過非対応のPBRでは、このVolumeの色は現れません。

透明球とすりガラス球は閉じたUV球、くさび形は閉じた三角柱、比較板は閉じた直方体です。入れ子や交差を扱わない初回のため、ガラスの包囲箱も互いに離してあります。

## 保存データと合成

`SourceArt/Blender/BSDFMaterials/BSDFMaterials.blend`が編集正本です。追加物だけを`Assets/Models/BSDFMaterials`へglTF／BIN／PNGとして書き出します。既存Roomを複製したglTFは作りません。

`Assets/Scenes/BSDFMaterials/BSDFMaterials.json`はRoom設定、新規glTF、カメラ、取り込む既存Instance IDを参照します。`LoadBSDFScene`がCPUで合成します。Labでは既存の箱・金属球を除きShaderBallを残し、Thicknessでは板の観察を優先してShaderBallも配置しません。

Scene／Node／Mesh／Primitive／Materialは安定IDで識別します。格子背景は両Sceneで同じMeshを共有し、配置IDはSceneごとに区別します。Manifestには配布物・正本・スクリプトと、流用するRoomの参照元ハッシュを保存します。

座標はglTFの右手系・Y-up・mです。CPUローダーが左手系へ変換します。Albedo／EmissiveはsRGB、Normal／ORMは線形です。ガラスにはバンプを与えず、形状の法線を使います。不透明な参照物のNormal Mapは従来通りです。

## 対応するマテリアルプロファイル

`KHR_materials_transmission`、`KHR_materials_ior`、`KHR_materials_volume`の、このアセットで使う定数値に対応します。Transmissionは0／1、透過マテリアルは非金属・白いBase Color・厚みあり・OPAQUEです。透過／厚みテクスチャ、薄膜、部分透過係数、金属透過、他のPrincipled機能は対象外です。

glTFの`thicknessFactor`は媒質境界の判定に使い、内部距離は実際の交差位置から計算します。新プロファイルは正の非一様Nodeスケールも扱います。既存Workshop／PathTracingプロファイルの制約は維持します。

## 編集と検証

初回生成は`generate.py`です。保存済み正本を編集する場合は、正本を開いて`export.py`を実行します。生成スクリプトは既存正本を上書きしません。

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
& $blender --background SourceArt/Blender/BSDFMaterials/BSDFMaterials.blend --python-exit-code 1 --python SourceArt/Blender/BSDFMaterials/export.py
& 'C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe' SourceArt/Blender/BSDFMaterials/validate.py --manifest
.\Tools\TestBSDFAssets.ps1
.\Tools\TestBSDFCpu.ps1
```

glTF-Validator、索引・UV・法線・接線、閉メッシュと向き、ID、参照・ハッシュ、隔離した正本からの再書出しを確認します。実行時にBlenderは必要ありません。自作データはMIT License、Copyright (c) 2026 masafumiです。
