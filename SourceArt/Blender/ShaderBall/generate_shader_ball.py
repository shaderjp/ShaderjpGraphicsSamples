"""Create the editable Shader Ball source. Run with Blender 5.2 --background.

The saved .blend is the authoritative source after manual edits. Generation
refuses to replace an existing source unless --overwrite-source is supplied.
MIT License, Copyright (c) 2026 masafumi.
"""

import argparse
import math
from pathlib import Path
import sys

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shader_ball_tools import (MATERIAL_NAME, MESH_NAME, TEXTURE_NAMES, build_material,
                               image_node, prepare_render, studio_setup, write_png)


def mesh_geometry():
    vertices, faces, face_uvs, smooth = [], [], [], []
    segments, stacks = 80, 48
    def vertex(position):
        vertices.append(position)
        return len(vertices) - 1
    def face(indices, uvs, is_smooth=True):
        faces.append(indices)
        face_uvs.append(uvs)
        smooth.append(is_smooth)
    def sphere_uv(s, t):
        return (0.02 + 0.96 * s, 0.36 + 0.62 * t)
    top = vertex((0, 0, 1.16))
    rings = []
    for i in range(1, stacks):
        theta = math.pi * i / stacks
        # Two shallow grooves are actual geometry and can cast shadows later.
        radius = 0.48 - 0.014 * math.exp(-((theta - 1.10) / 0.075) ** 2)
        radius -= 0.018 * math.exp(-((theta - 1.95) / 0.075) ** 2)
        rings.append([vertex((radius * math.sin(theta) * math.cos(2 * math.pi * j / segments),
                              radius * math.sin(theta) * math.sin(2 * math.pi * j / segments),
                              0.68 + radius * math.cos(theta))) for j in range(segments)])
    bottom = vertex((0, 0, 0.20))
    for j in range(segments):
        k = (j + 1) % segments
        s0, s1 = j / segments, (j + 1) / segments
        face((top, rings[0][j], rings[0][k]),
             (sphere_uv((s0 + s1) / 2, 1), sphere_uv(s0, 1 - 1 / stacks), sphere_uv(s1, 1 - 1 / stacks)))
        for i in range(len(rings) - 1):
            t0, t1 = 1 - (i + 1) / stacks, 1 - (i + 2) / stacks
            face((rings[i][j], rings[i + 1][j], rings[i + 1][k], rings[i][k]),
                 (sphere_uv(s0, t0), sphere_uv(s0, t1), sphere_uv(s1, t1), sphere_uv(s1, t0)))
        face((rings[-1][j], bottom, rings[-1][k]),
             (sphere_uv(s0, 1 / stacks), sphere_uv((s0 + s1) / 2, 0), sphere_uv(s1, 1 / stacks)))
    profile = [(0.565, 0), (0.60, 0.025), (0.60, 0.095), (0.575, 0.125),
               (0.31, 0.155), (0.265, 0.19), (0.265, 0.225)]
    arc = [0.0]
    for a, b in zip(profile, profile[1:]):
        arc.append(arc[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    base_rings = [[vertex((r * math.cos(2 * math.pi * j / segments),
                           r * math.sin(2 * math.pi * j / segments), z)) for j in range(segments)]
                  for r, z in profile]
    for j in range(segments):
        k = (j + 1) % segments
        for i in range(len(profile) - 1):
            v0, v1 = 0.04 + 0.26 * arc[i] / arc[-1], 0.04 + 0.26 * arc[i + 1] / arc[-1]
            u0, u1 = 0.02 + 0.68 * j / segments, 0.02 + 0.68 * (j + 1) / segments
            face((base_rings[i][j], base_rings[i][k], base_rings[i + 1][k], base_rings[i + 1][j]),
                 ((u0, v0), (u1, v0), (u1, v1), (u0, v1)))
    for ring, z, center_uv, radii, is_bottom in (
        (base_rings[0], 0, (0.855, 0.14), (0.10, 0.10), True),
        (base_rings[-1], 0.225, (0.855, 0.29), (0.10, 0.045), False),
    ):
        center = vertex((0, 0, z))
        for j in range(segments):
            k = (j + 1) % segments
            def uv(index):
                angle = 2 * math.pi * index / segments
                return (center_uv[0] + radii[0] * math.cos(angle), center_uv[1] + radii[1] * math.sin(angle))
            indices = (center, ring[k], ring[j]) if is_bottom else (center, ring[j], ring[k])
            uvs = (center_uv, uv(k), uv(j)) if is_bottom else (center_uv, uv(j), uv(k))
            face(indices, uvs, False)
    mesh = bpy.data.meshes.new(MESH_NAME + "_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon, uvs, is_smooth in zip(mesh.polygons, face_uvs, smooth):
        polygon.use_smooth = is_smooth
        for index, uv in zip(polygon.loop_indices, uvs):
            uv_layer.data[index].uv = uv
    obj = bpy.data.objects.new(MESH_NAME, mesh)
    collection = bpy.data.collections.new("EXPORT")
    bpy.context.scene.collection.children.link(collection)
    collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    modifier = obj.modifiers.new("TriangulateBeforeBake", "TRIANGULATE")
    modifier.quad_method = "FIXED"
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return obj


def rgba(rgb):
    out = np.empty((1024, 1024, 4), dtype=np.uint8)
    out[:, :, :3] = np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    out[:, :, 3] = 255
    return out


def generate_textures(directory):
    yy, xx = np.mgrid[0:1024, 0:1024]
    u, v = (xx + 0.5) / 1024, 1 - (yy + 0.5) / 1024
    s, t = (u - 0.02) / 0.96, (v - 0.36) / 0.62
    sphere = (v >= 0.34)
    metal = sphere & (((s > 0.12) & (s < 0.28)) | ((s > 0.60) & (s < 0.77)))
    metal |= (~sphere) & (v > 0.19)
    albedo = np.zeros((1024, 1024, 3), dtype=np.float32)
    albedo[:] = (0.09, 0.14, 0.20)
    albedo[sphere] = (0.17, 0.43, 0.52)
    albedo[metal] = (0.81, 0.59, 0.28)
    albedo[sphere & (s > 0.60) & (s < 0.77)] = (0.72, 0.77, 0.82)
    # Orientation marks are pigment, not baked shading. The arrow points upward.
    shaft = sphere & (abs(s - 0.44) < 0.012) & (t > 0.38) & (t < 0.63)
    arrow = sphere & (t >= 0.63) & (t <= 0.70) & (abs(s - 0.44) < (0.70 - t) * 0.65)
    stripe = sphere & (t > 0.83) & (t < 0.86)
    albedo[shaft | arrow | stripe] = (0.92, 0.94, 0.93)
    albedo[sphere & (t > 0.14) & (t < 0.18)] = (0.95, 0.29, 0.08)
    # Small blue squares differentiate the bottom from the top.
    squares = sphere & (t > 0.22) & (t < 0.26) & ((s * 24) % 1 < 0.55)
    albedo[squares] = (0.025, 0.08, 0.21)
    roughness = np.full((1024, 1024), 0.65, dtype=np.float32)
    roughness[sphere] = 0.52
    roughness[metal] = 0.18
    roughness[sphere & (s > 0.60) & (s < 0.77)] = 0.38
    roughness[sphere & (s > 0.80)] = 0.80
    roughness[shaft | arrow] = 0.42
    emissive = np.zeros_like(albedo)
    glow = sphere & (abs(s - 0.44) < 0.030) & (t > 0.28) & (t < 0.30)
    emissive[glow] = (1.0, 0.19, 0.035)
    # Height is an authoring-only texture baked into a tangent-space normal map.
    height = 0.5 + 0.08 * np.sin(s * math.tau * 70) * np.sin(t * math.tau * 45)
    height[metal] = 0.5 + 0.025 * np.sin(s[metal] * math.tau * 160)
    height[shaft | arrow] += 0.12
    height[~sphere] = 0.5 + 0.015 * np.sin(u[~sphere] * math.tau * 120)
    values = {"Albedo": albedo, "Metallic": np.repeat(metal[:, :, None], 3, axis=2),
              "Roughness": np.repeat(roughness[:, :, None], 3, axis=2), "Emissive": emissive,
              "BakeHeight": np.repeat(height[:, :, None], 3, axis=2)}
    images = {}
    for name, pixels in values.items():
        filename = directory / (name + ".png")
        write_png(filename, rgba(pixels))
        image = bpy.data.images.load(str(filename), check_existing=False)
        image.name = name
        image.colorspace_settings.name = "sRGB" if name in ("Albedo", "Emissive") else "Non-Color"
        images[name] = image
    return images


def bake_maps(obj, images, directory):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    scene.cycles.seed = 0
    scene.render.bake.margin = 16
    scene.render.bake.use_selected_to_active = False
    scene.render.image_settings.color_depth = "8"
    bake_material = bpy.data.materials.new("AUTHORING_BAKE")
    bake_material.use_nodes = True
    nodes, links = bake_material.node_tree.nodes, bake_material.node_tree.links
    nodes.clear()
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    output = nodes.new("ShaderNodeOutputMaterial")
    links.new(shader.outputs[0], output.inputs["Surface"])
    height = image_node(nodes, "BakeHeight", images["BakeHeight"], (-500, 0))
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.65
    bump.inputs["Distance"].default_value = 0.003
    links.new(height.outputs["Color"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    obj.data.materials.append(bake_material)
    for name, bake_type in (("Normal", "NORMAL"), ("AO", "AO")):
        print(f"Baking {name} (1024 x 1024, Cycles CPU, 64 samples)", flush=True)
        image = bpy.data.images.new(name, width=1024, height=1024, alpha=True)
        image.colorspace_settings.name = "Non-Color"
        image.file_format = "PNG"
        image.filepath_raw = str(directory / (name + ".png"))
        target = image_node(nodes, "BakeTarget_" + name, image, (-500, -300))
        nodes.active = target
        for node in nodes:
            node.select = node == target
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.bake(type=bake_type, use_clear=True, normal_space="TANGENT")
        pixels = np.empty(1024 * 1024 * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        pixels = pixels.reshape((-1, 4))
        empty = pixels[:, 3] < 0.5
        pixels[empty, :3] = (0.5, 0.5, 1.0) if name == "Normal" else (1.0, 1.0, 1.0)
        if name == "Normal":
            # The bake margin interpolates normal colors; renormalize before
            # quantizing so the padded pixels are valid tangent normals too.
            vectors = pixels[:, :3] * 2 - 1
            lengths = np.linalg.norm(vectors, axis=1)
            vectors[lengths < 1e-6] = (0, 0, 1)
            lengths[lengths < 1e-6] = 1
            pixels[:, :3] = (vectors / lengths[:, None] + 1) * 0.5
        pixels[:, 3] = 1.0
        image.pixels.foreach_set(pixels.ravel())
        image.update()
        # Store raw linear channel values with no PNG gamma/profile conversion.
        write_png(Path(image.filepath_raw), rgba(pixels.reshape((1024, 1024, 4))[::-1, :, :3]))
        # Generated images do not preserve their pixel buffer across .blend
        # reloads. Make the raw PNG the file source before packing it.
        image.source = "FILE"
        image.reload()
        images[name] = image
        nodes.remove(target)
    # Pack the exact baked AO bytes with independently authored metallic/roughness.
    # Load pixels as non-color; Blender's pixel array has bottom-to-top rows.
    arrays = {}
    for name in ("AO", "Roughness", "Metallic"):
        image = images[name]
        pixels = np.empty(1024 * 1024 * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        arrays[name] = pixels.reshape((1024, 1024, 4))[::-1, :, 0]
    write_png(directory / "ORM.png", rgba(np.stack([arrays[n] for n in ("AO", "Roughness", "Metallic")], axis=2)))
    images["ORM"] = bpy.data.images.load(str(directory / "ORM.png"), check_existing=False)
    images["ORM"].name = "ORM"
    images["ORM"].colorspace_settings.name = "Non-Color"
    obj.data.materials.clear()
    bpy.data.materials.remove(bake_material)
    return images


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--overwrite-source", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    if bpy.app.version[:2] != (5, 2):
        raise RuntimeError("Use Blender 5.2 LTS for reproducible source generation")
    source = args.source_dir.resolve()
    source.mkdir(parents=True, exist_ok=True)
    blend = source / "ShaderBall.blend"
    if blend.exists() and not args.overwrite_source:
        raise RuntimeError(f"Refusing to overwrite edited source {blend}; export the saved blend instead")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene["asset_license"] = "MIT License, Copyright (c) 2026 masafumi"
    scene["asset_generator"] = "Shader.jp Shader Ball 1.0"
    scene["blender_version"] = bpy.app.version_string
    texture_work = source / ".generated"
    texture_work.mkdir(parents=True, exist_ok=True)
    obj = mesh_geometry()
    images = bake_maps(obj, generate_textures(texture_work), texture_work)
    material = build_material(images)
    obj.data.materials.append(material)
    for name, image in images.items():
        # Standalone teaching maps and the authoring height map have no material
        # users. Keep them in the canonical blend even after closing Blender.
        image.use_fake_user = True
        png_bytes = (texture_work / (name + ".png")).read_bytes()
        image.pack(data=png_bytes, data_len=len(png_bytes))
        images[name].filepath = "//.generated/" + name + ".png"
    studio_setup()
    prepare_render()
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_distance = 2.8
                area.spaces.active.region_3d.view_location = (0, 0, 0.58)
                area.spaces.active.shading.type = "MATERIAL"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    print(f"Saved {blend}: {len(obj.data.polygons)} triangles, packed images, one export mesh", flush=True)


if __name__ == "__main__":
    main()
