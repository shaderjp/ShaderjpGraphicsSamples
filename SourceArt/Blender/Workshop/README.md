# Workshopの編集正本

Blender 5.2.1 LTSで制作した、自作の工房・ショールームです。Workshop.blendにStandard／Stressの静的な2 Sceneと梱包PNG、WorkshopSky.blendに形状・ライトを焼き込まない外部環境を保存しています。

[制作・配布仕様、画像、拡張ローダーの接続条件](../../../Docs/Assets/Workshop.md)を参照してください。

| スクリプト | 役割 |
|---|---|
| generate.py | 新規生成専用。保存済み正本があれば拒否する |
| export.py | 保存済み正本をglTF／BIN／PNG／設定へ書き出す。正本を保存しない |
| bake.py | 保存済みSkyから線形EXRとDDS IBLを生成する |
| render_references.py | 保存済み形状と画像でCycles参考画像を撮影する |
| validate.py | 形状、共有参照、Alpha、接線、境界、カメラ、IBL、ハッシュを検査する |
| workshop_tools.py | 制作用の形状・材質・PNG・カメラ共通処理 |

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$workshopPython = Join-Path (Split-Path $blenderExe -Parent) '5.2/python/bin/python.exe'
& $blenderExe --background SourceArt/Blender/Workshop/Workshop.blend --python-exit-code 1 --python SourceArt/Blender/Workshop/export.py
& $blenderExe --background --factory-startup --python-exit-code 1 --python SourceArt/Blender/Workshop/bake.py
& $blenderExe --background SourceArt/Blender/Workshop/Workshop.blend --python-exit-code 1 --python SourceArt/Blender/Workshop/render_references.py
& $workshopPython SourceArt/Blender/Workshop/validate.py --manifest
.\Tools\TestWorkshopAssets.ps1
```

通常の編集にgenerate.pyを使わないでください。Mesh・Material・Imageの共有を維持し、Standard／Stress両方の保存済み配置を書き出します。カメラと材質のカスタム設定の更新方法は共通章に記載しています。画像は保存・梱包してからblendを保存します。

初期生成、書出し、ベイク、レンダー、検証は分離しています。描画アプリはBlenderに依存しません。GPUリソースとMeshlet・レイトレーシング用データは後続サンプルで追加します。

MIT License, Copyright (c) 2026 masafumi。外部モデル・写真・HDRIは使用していません。
