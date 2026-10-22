# モーション制作正本

`SilverRobotMotionSet.blend`は旧Animated版を保持した追加の正本です。Library 1 Standardから取り込んだ地上移動・通常ジャンプと、Blenderで補った発進・停止・旋回の計13クリップをTRSで保存しています。通常はこのblendを編集してexport.pyを実行します。

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background SourceArt/Blender/MotionMatching/SilverRobotMotionSet.blend --python-exit-code 1 --python SourceArt/Blender/MotionMatching/export.py
./Tools/TestMotionAssets.ps1
```

Rootの配置はBlenderで見える状態です。出力時に水平軌跡とYawを別データへ取り出し、スキニング用glTFから抽出済みの成分を除きます。出力操作は正本を保存しません。

初回の制作を再現するときだけ、Blender同梱Pythonで`Tools/AcquireMotionSources.py`、Blenderで`generate.py`を実行します。generate.pyは既存正本の上書きを拒否します。元配布の一覧と固定ハッシュはSourceInventory.jsonにあります。元ZIPは`.cache/motion-matching`、使用した動作はblendと配布glTFに保存します。

新しい外部コード依存はありません。モデル・制作コードはMIT、Quaternius由来の動作は同梱のCC0表示に従って区別しています。[学習順と工程](../../../Docs/MotionMatchingRoadmap.md)
