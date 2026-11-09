# CullingLabの編集正本

基本31〜34の視錐台・遮蔽判定を比較する専用シーンです。Blender 5.2.1 LTSで制作した壁、開口部、柱、扉に、Workshopの機械部品・作業台・格子を組み合わせています。既存Workshop正本は変更していません。

`CullingLab.blend`を編集・保存してから`export.py`を実行します。`generate.py`は初回生成専用で、既存正本を上書きしません。書出し処理は正本を保存しません。

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background SourceArt/Blender/CullingLab/CullingLab.blend --python-exit-code 1 --python SourceArt/Blender/CullingLab/export.py
& 'C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe' SourceArt/Blender/CullingLab/validate.py
& 'C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe' SourceArt/Blender/CullingLab/validate.py --reexport
```

Mini／Standard／Stressは64／512／4,096個の検査対象を持ちます。壁・床・柱・扉は別に数えます。Objectの`id`と`culling_role`を保持してください。`object:`は検査対象、`occluder:`は不透明な実形状、`door:`はX方向に動かす遮蔽物です。画像は正本に梱包し、形状・材質の共有参照を維持します。

glTFは右手系Y-up、実行時は既存ローダーで一度だけ左手系へ変換します。書出し先はAssetsのCullingLab、環境IBLはWorkshopSkyを参照します。実行時のBlender依存はありません。

`--reexport`は`.cache/culling-reexport`へ正本と必要な制作ヘルパーをコピーし、隔離した書出しのglTF／BIN／PNG・カメラ設定を既存成果物とバイト比較します。元の正本やAssetsを上書きしません。

MIT License, Copyright (c) 2026 masafumi。外部モデル・画像は使用していません。
