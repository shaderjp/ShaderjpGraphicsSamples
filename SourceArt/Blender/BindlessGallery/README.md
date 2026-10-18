# BindlessGalleryの編集正本

Blender 5.2.1 LTSの工業技術ミュージアムです。64種類の形状、256材質、400枚の512×512 PNGを含みます。保存済みの正本には全画像を梱包しており、`.generated`は再書出しに不要です。

`generate.py`は新規生成専用で、保存済み正本が存在すると拒否します。通常はBlenderで形状・配置・画像・材質を編集し、正本を保存してから`export.py`を使用します。書出しと参考レンダーは正本を保存しません。

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blenderExe --background SourceArt/Blender/BindlessGallery/BindlessGallery.blend --python-exit-code 1 --python SourceArt/Blender/BindlessGallery/export.py
& $blenderExe --background SourceArt/Blender/BindlessGallery/BindlessGallery.blend --python-exit-code 1 --python SourceArt/Blender/BindlessGallery/render_references.py
& 'C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe' SourceArt/Blender/BindlessGallery/validate.py --manifest
.\Tools\TestBindlessGalleryAssets.ps1
```

画像・材質の対応は編集可能なTextureノードと`profile`、配置はObjectの`geometry`／`material`／`id`、設定はStandard Sceneの`gallery_config`に保存します。正本の64個のMeshデータをScene間で共有します。Object側の材質スロットにより、形状を複製せず材質を変更します。

書出し後のglTFは標準的な材質別Mesh参照を持ち、すべての材質バリエーションで同じGeometryのAccessorを共有します。Nodeの変換はBlender Z-upからglTFの右手Y-upへ一度だけ変換します。実行時の左手系変換は既存Workshop CPUローダーが担当します。

[アセット仕様・画像](../../../Docs/Assets/BindlessGallery.md)。MIT License, Copyright (c) 2026 masafumi。外部モデル・画像は使用していません。

書出しでは各Sceneへ切り替えた後にView Layerを更新し、評価済みのWorld変換を使います。非アクティブSceneの未評価行列を使って全Objectが原点へ集まる問題を防ぐため、検証側でも配置の広がりと退行用の不正データを確認します。
