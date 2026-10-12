# Render-to-Texture用のモニターアセット

第14〜16回はLighting Labへ板状モニター1個を加えます。glTFは不透明・閉じた形状、1 Node・1 Mesh・1 Primitive・1 Material、12三角形です。寸法は1.4×1.4×0.08 mで、各面のUVは映像を0〜1で参照します。

## 正本と書出し

編集正本は`SourceArt/Blender/PostProcess/Monitor.blend`です。配布モデルは`Assets/Models/PostProcess/Monitor`へ保存します。以前のLighting Lab正本・配布データを変更せず、別の制作工程として管理します。

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blenderExe --background SourceArt/Blender/PostProcess/Monitor.blend --python-exit-code 1 --python SourceArt/Blender/PostProcess/monitor.py
.\Tools\TestPostProcessAssets.ps1
```

保存済み正本からの書出しは正本を保存し直しません。初期生成の`--create`は既存正本があると拒否します。編集ではBlenderで正本を保存してから書き出します。Blender 5.2.1 LTSを使い、材質画像を正本へ梱包しています。

## 配置と描画上の指定

`Assets/Scenes/PostProcess/Exhibition.json`は、Lighting Labのモデルと環境を参照します。`secondaryCamera`に映像用カメラを、モニターの`renderTexture: true`に映像を読む描画対象を指定します。glTFの材質を複数Primitiveへ分ける対応は追加しません。プログラム側でモニターの描画をUnlitへ切り替えます。

このモニターは方向光のShadow対象から外します。映像取得パスでも除外し、現在描いている映像を同じパス内で読む循環を避けます。モデルと配置は右手系・Y-up・m、サンプルはCPUで左手系へ変換します。

glTF-Validatorのエラー・警告0件、再書出しの一致、Blender正本のSHA-256が変わらないことを検証します。新規モデルとスクリプト、画像は自作のMIT License、Copyright (c) 2026 masafumiです。
