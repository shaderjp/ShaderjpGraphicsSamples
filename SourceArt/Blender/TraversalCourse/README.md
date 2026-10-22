# 衝突・段差・ジャンプの練習コース

Blender 5.2.1 LTSの保存済み`TraversalCourse.blend`を編集してexport.pyを実行します。generate.pyは初回作成用で、既存正本を上書きしません。

- Render：床、20 cm段差、65 cm障害物、台、回転した壁。
- Collision：障害物IDを持つ箱Empty。半寸法はscaleで、回転はYawだけを編集します。
- Targets：Spawnとチェックポイント。

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background SourceArt/Blender/TraversalCourse/TraversalCourse.blend --python-exit-code 1 --python SourceArt/Blender/TraversalCourse/export.py
./Tools/TestMotionAssets.ps1
```

export.pyは描画用のWorkshop形式と判定用Traversal.jsonを同時に出力します。正本は保存せず、glTFの接線と小さな共通PNGをオフラインで作成します。実行サンプルはBlenderに依存しません。レベルと制作コードはリポジトリのMITライセンスです。[解説](../../../Docs/TraversalController.md)
