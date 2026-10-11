"""Bake edited mesh/UV/height, then save to an explicitly chosen .blend.

Does not regenerate mesh, albedo, metallic, roughness or emissive.
MIT License, Copyright (c) 2026 masafumi.
"""

import argparse
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_shader_ball import bake_maps
from shader_ball_tools import MESH_NAME, source_images


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-blend", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    if bpy.app.version[:2] != (5, 2) or not bpy.data.filepath:
        raise RuntimeError("Open the edited source with Blender 5.2 LTS")
    obj = bpy.data.objects[MESH_NAME]
    if len(obj.data.materials) != 1 or obj.modifiers or len(obj.data.uv_layers) != 1:
        raise RuntimeError("Apply modifiers, keep one UV map and one material before baking")
    if any(len(p.vertices) != 3 for p in obj.data.polygons) or len(obj.data.polygons) > 20000:
        raise RuntimeError("Triangulate the edited mesh and keep <=20000 triangles")
    if any(abs(v - 1) > 1e-6 for v in obj.scale):
        raise RuntimeError("Apply scale before baking")
    images = source_images()
    images["BakeHeight"] = bpy.data.images.get("BakeHeight")
    if images["BakeHeight"] is None:
        raise RuntimeError("The packed authoring-only BakeHeight image is required")
    material = obj.data.materials[0]
    obj.data.materials.clear()
    for name in ("Normal", "AO", "ORM"):
        bpy.data.images.remove(images.pop(name), do_unlink=True)
    output = args.output_blend.resolve()
    directory = output.parent / ".generated"
    directory.mkdir(parents=True, exist_ok=True)
    preview = [(o, o.hide_render) for o in bpy.data.collections["PREVIEW_ONLY"].objects]
    for other, _ in preview:
        other.hide_render = True
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    try:
        images = bake_maps(obj, images, directory)
    finally:
        for other, hidden in preview:
            other.hide_render = hidden
    obj.data.materials.append(material)
    for name in ("Normal", "ORM"):
        material.node_tree.nodes["Texture_" + name].image = images[name]
    for name in ("Normal", "AO", "ORM"):
        raw = (directory / (name + ".png")).read_bytes()
        images[name].pack(data=raw, data_len=len(raw))
        images[name].use_fake_user = True
        images[name].filepath = "//.generated/" + name + ".png"
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    print(f"Rebaked edited mesh and saved {output}", flush=True)


if __name__ == "__main__":
    main()
