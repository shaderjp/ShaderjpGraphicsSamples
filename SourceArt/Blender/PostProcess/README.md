# Monitorの編集と再書出し

Monitor.blendはBlender 5.2.1 LTSの編集正本です。寸法はBlender Z-upでX=1.4、Y=0.08、Z=1.4 m。glTFへY-upで書き出すと1.4×1.4×0.08 mです。各面に0〜1のUVと、閉じた12三角形を持ちます。

通常は正本をBlenderで編集・保存し、リポジトリルートから次を実行します。

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background SourceArt/Blender/PostProcess/Monitor.blend --python-exit-code 1 --python SourceArt/Blender/PostProcess/monitor.py
.\Tools\TestPostProcessAssets.ps1
```

monitor.pyは正本を保存し直さず、梱包されたPNGとメッシュを書き出します。新規正本の初期生成だけは`--factory-startup ... -- --create`を使い、既存Monitor.blendがあると拒否します。`--output`で一時的なAssetsルートを指定して再現性を確認できます。

モデルは1 Node・1 Mesh・1 Primitive・1 Material。透過、アルファ抜き、ノード階層、UnlitのglTF拡張は使いません。アプリ側のrenderTexture指定で、映像TextureをUnlitとして描きます。

自作、MIT License、Copyright (c) 2026 masafumi。
