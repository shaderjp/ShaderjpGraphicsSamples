# パストレーシングシリーズの編集正本

Blender 5.2.1 LTSの`PathTracing.blend`にRoom、2カメラ、梱包済みPNGを保存しています。赤緑の壁、白い箱、金属球、天井面光源は自作です。ShaderBallは既存の保存済み正本から取り込み、元データを変更していません。全データはMIT License、Copyright (c) 2026 masafumiです。

初回生成は`generate.py`、通常の書出しは保存済みblendを開いて`export.py`を使います。生成スクリプトは既存正本を上書きしません。実行サンプルはBlenderに依存しません。

```powershell
$blenderExe = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
& $blenderExe --background SourceArt/Blender/PathTracing/PathTracing.blend --python-exit-code 1 --python SourceArt/Blender/PathTracing/export.py
& 'C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe' SourceArt/Blender/PathTracing/validate.py --manifest
.\Tools\TestPathTracingAssets.ps1
```

glTFの座標は右手系・Y-up・m、画像はAlbedo／EmissiveがsRGB、Normal／ORMが線形です。ShaderBallの画像も保存したblendに梱包し、再書出し時に元画像の作業フォルダを要求しません。配置と材質は安定IDで識別し、シリーズの各サンプルから同じ配布ファイルを参照します。Manifestには元ShaderBallの正本・glTFのハッシュも記録します。

天井の発光メッシュそのものが照明です。代理の点光源、焼き込んだIBL、AOは使用しません。新しい配布先とカメラの仕様は[アセット解説](../../../Docs/Assets/PathTracing.md)を参照してください。
