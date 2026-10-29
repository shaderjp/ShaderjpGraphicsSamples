# Geometry Lab 正本

`GeometryLab.blend`は発展20〜23の自作データです。MIT Licenseで配布します。

Blender 5.2で次を実行すると、正本と実行用バイナリ、JSON、SHA-256 Manifestを同じ固定seed `20261029`で再生成できます。

```powershell
blender --background --python SourceArt/Blender/GeometryLab/generate.py
```

実行用データは左手系・Y上向き・メートルです。Blender上では `(x, -z, y)`へ変換しています。バイナリはlittle endianで、各頂点はposition3、UV2、normal3、tangent4の48 bytes、インデックスはuint32です。実行時にBlenderは必要ありません。

レリーフ彫刻は65,536三角形、薄板彫刻は16,384三角形です。床・台座・可動パネルも同じ生成スクリプトで制作しています。発展22の規則タイルはCPUでトポロジーを生成し、GPUで曲面位置と法線を評価します。

PBRテクスチャとIBLには既存Workshopの自作MITデータを再利用します。GPU上の正しい構築を調べる試験用の平面・格子は実演用モデルから独立して生成します。
