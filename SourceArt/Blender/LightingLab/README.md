# Lighting Labの編集正本と再生成

Blender 5.2.1 LTSで制作した自作アセットです。LightingLab.blendに17個の配布モデルと配置設定、Studio.blend／Room.blendにHDR環境の撮影用形状・照明・パノラマカメラを保存しています。画像は梱包済みです。既存ShaderBall.blendは変更せず、参考レンダー時に読み込んで使用します。

[配布仕様・図・操作手順](../../../Docs/Assets/LightingLab.md)を参照してください。以下はリポジトリルートから実行します。

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$blenderPython = Join-Path (Split-Path $blenderExe -Parent) '5.2/python/bin/python.exe'
& $blenderExe --background SourceArt/Blender/LightingLab/LightingLab.blend --python-exit-code 1 --python SourceArt/Blender/LightingLab/export.py
& $blenderExe --background --factory-startup --python-exit-code 1 --python SourceArt/Blender/LightingLab/bake.py
& $blenderPython SourceArt/Blender/LightingLab/validate.py --manifest
.\Tools\TestLightingLabAssets.ps1
& $blenderExe --background SourceArt/Blender/LightingLab/LightingLab.blend --python-exit-code 1 --python SourceArt/Blender/LightingLab/render_references.py
```

モデル編集時はUV0、三角形、接線を出せる形状、1モデル・1材質、不透明を維持します。PNGはAlbedo／Normal／ORM／Emissiveの4枚で、画像編集後は保存して梱包し直します。配置は各オブジェクトのasset_nameとSceneのscene_jsonで管理します。配布するノードには配置変換を含めず、位置・回転・スケールをJSONへ渡します。回転はdegreeのpitch／yaw／roll、正のscaleのみを使います。

`generate.py`は新規生成専用で、保存済み正本がある場合は拒否します。通常は編集した正本から`export.py`で書き出します。`export.py`と`render_references.py`は正本を保存しません。`bake.py --reuse-exr`は現在の線形EXRからDDSだけを再計算し、`--environment Studio`／`Room`は片方だけを更新します。`--force-lut`はBRDF LUTも再計算します。

標準のIBL積分はHammersley列の1024 samples、GGX alpha=roughness²、height-correlated Smithです。DDS writerはDX10ヘッダー付きのRGBA16F／RG16Fだけを生成し、6面・mip・byte数・有限値を検査します。IrradianceにはEを保存し、実行シェーダで1／πを掛けます。PNGの表示用画像をHDR照明として流用しません。

生成設定とハッシュはAssets/Scenes/LightingLab/Manifest.jsonに記録します。正本・環境・積分スクリプトを変えた場合は、再生成後にManifestも更新してください。配布アプリは生成済みデータを読むため、Blenderを必要としません。

MIT License, Copyright (c) 2026 masafumi。写真・外部HDRI・追加Pythonパッケージを使用していません。
