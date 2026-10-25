# BSDF Materialsの編集正本

Blender 5.2.1 LTSで保存した`BSDFMaterials.blend`に、追加するガラスと格子背景、Lab／ThicknessのScene、カメラ、梱包済みPNGを収録しています。元のPathTracing Roomは変更せず、実行時にCPUローダーで合成します。正本を開いた際に表示されるのは追加物です。

初回生成は`generate.py`、通常の編集後の書出しは`export.py`を使います。保存した正本を再生成で上書きしません。書出しは正本を保存し直さず、配布glTF／BIN／画像とScene設定を更新します。

```powershell
$blender = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
& $blender --background SourceArt/Blender/BSDFMaterials/BSDFMaterials.blend --python-exit-code 1 --python SourceArt/Blender/BSDFMaterials/export.py
& 'C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe' SourceArt/Blender/BSDFMaterials/validate.py --manifest
.\Tools\TestBSDFAssets.ps1
```

ガラスはMetallic 0、Transmission 1、IOR 1.5、白いBase Color、OPAQUEです。`profile`と`bsdf`のカスタムプロパティを正本に保存し、書出し時に教材用のglTF定数へ対応付けます。表面のPrincipled設定も確認できますが、Blenderの複合BSDF全機能を実行サンプルへ持ち込むものではありません。

吸収付きガラスは`attenuationColor = (0.18, 0.65, 0.85)`、`attenuationDistance = 0.5 m`です。吸収なしではdistanceを0として保存し、glTFの該当プロパティを省略します。実行時は0を「吸収なし」の内部表現として扱います。

物性を変更する場合はマテリアルの`bsdf` JSONも更新してください。粗さなど不透明PBRの定数は`profile`を更新します。カメラと流用するRoomのIDはLab Sceneの`bsdf_config`へ保存しています。

モデルは閉じた外向きのガラス形状と、不透明な格子背景です。UVの継ぎ目を位置で結合して閉メッシュを検証し、法線・接線・IDを確認します。隔離した保存済みblendからの再書出しを試験し、実行時の生成を行いません。

配布物と正本・スクリプトのSHA-256は`Assets/Scenes/BSDFMaterials/Manifest.json`です。元Roomの参照元ハッシュも含めます。ハッシュ対象のテキストはLFで保存します。

自作データ・コードはMIT License、Copyright (c) 2026 masafumiです。
