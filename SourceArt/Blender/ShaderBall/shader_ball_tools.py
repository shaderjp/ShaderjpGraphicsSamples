"""Shared Blender helpers for the Shader.jp Shader Ball. MIT License."""

import math
from pathlib import Path
import struct
import zlib

import bpy
from mathutils import Vector

TEXTURE_NAMES = ("Albedo", "Normal", "Metallic", "Roughness", "AO", "ORM", "Emissive")
MESH_NAME = "ShaderBall"
MATERIAL_NAME = "ShaderBall_Material"


def write_png(path, pixels):
    """Write top-to-bottom uint8 RGBA pixels without a display/view transform."""
    height, width, channels = pixels.shape
    assert channels == 4
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    scanlines = b"".join(b"\0" + pixels[y].tobytes() for y in range(height))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def image_node(nodes, name, image, position):
    node = nodes.new("ShaderNodeTexImage")
    node.name = "Texture_" + name
    node.label = name
    node.image = image
    node.location = position
    node.interpolation = "Linear"
    node.extension = "REPEAT"
    return node


def build_material(images, mode="pbr"):
    material = bpy.data.materials.new(MATERIAL_NAME if mode == "pbr" else "ShaderBall_AlbedoPreview")
    material.use_nodes = True
    material.use_backface_culling = True
    nodes = material.node_tree.nodes
    nodes.clear()
    links = material.node_tree.links
    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (640, 100)
    albedo = image_node(nodes, "Albedo", images["Albedo"], (-640, 440))
    if mode == "albedo-preview":
        shader = nodes.new("ShaderNodeEmission")
        shader.inputs["Strength"].default_value = 1.0
        links.new(albedo.outputs["Color"], shader.inputs["Color"])
        links.new(shader.outputs[0], output.inputs["Surface"])
        return material
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.name = "PBR"
    shader.location = (320, 100)
    shader.inputs["Metallic"].default_value = 1.0 if mode == "pbr" else 0.0
    shader.inputs["Roughness"].default_value = 1.0 if mode == "pbr" else 0.5
    links.new(albedo.outputs["Color"], shader.inputs["Base Color"])
    links.new(shader.outputs[0], output.inputs["Surface"])
    if mode != "pbr":
        return material
    orm = image_node(nodes, "ORM", images["ORM"], (-640, 140))
    separate = nodes.new("ShaderNodeSeparateColor")
    separate.location = (-300, 140)
    links.new(orm.outputs["Color"], separate.inputs["Color"])
    links.new(separate.outputs["Green"], shader.inputs["Roughness"])
    links.new(separate.outputs["Blue"], shader.inputs["Metallic"])
    # The Blender glTF exporter recognizes this group as the occlusion input.
    group = bpy.data.node_groups.get("glTF Material Output")
    if group is None:
        group = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
        group.interface.new_socket(name="Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    occlusion = nodes.new("ShaderNodeGroup")
    occlusion.node_tree = group
    occlusion.location = (0, -140)
    links.new(separate.outputs["Red"], occlusion.inputs["Occlusion"])
    normal = image_node(nodes, "Normal", images["Normal"], (-640, -220))
    normal_map = nodes.new("ShaderNodeNormalMap")
    normal_map.space = "TANGENT"
    normal_map.inputs["Strength"].default_value = 1.0
    normal_map.location = (-100, -240)
    links.new(normal.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], shader.inputs["Normal"])
    emissive = image_node(nodes, "Emissive", images["Emissive"], (-640, -540))
    shader.inputs["Emission Strength"].default_value = 1.0
    links.new(emissive.outputs["Color"], shader.inputs["Emission Color"])
    return material


def source_images():
    """The packed named images in the saved blend are the export source."""
    images = {}
    for name in TEXTURE_NAMES:
        image = bpy.data.images.get(name)
        if image is None or tuple(image.size) != (1024, 1024):
            raise RuntimeError(f"Missing 1024 x 1024 source image: {name}")
        images[name] = image
    return images


def save_source_image(image, path):
    """Preserve packed PNG bytes; save edited image pixels through Blender."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if image.packed_file is not None and not image.is_dirty:
        path.write_bytes(bytes(image.packed_file.data))
    else:
        image.filepath_raw = str(path)
        image.file_format = "PNG"
        image.save()


def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def studio_setup():
    """Reference-render objects are outside the EXPORT collection."""
    studio = bpy.data.collections.new("PREVIEW_ONLY")
    bpy.context.scene.collection.children.link(studio)
    def move(obj):
        for collection in tuple(obj.users_collection):
            collection.objects.unlink(obj)
        studio.objects.link(obj)
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.002))
    floor = bpy.context.object
    floor.name = "Preview_Floor"
    move(floor)
    mat = bpy.data.materials.new("Preview_Floor_Material")
    mat.use_nodes = True
    mat.node_tree.nodes.clear()
    floor_shader = mat.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
    floor_output = mat.node_tree.nodes.new("ShaderNodeOutputMaterial")
    mat.node_tree.links.new(floor_shader.outputs[0], floor_output.inputs["Surface"])
    floor_shader.inputs["Base Color"].default_value = (0.055, 0.075, 0.09, 1)
    floor_shader.inputs["Roughness"].default_value = 0.8
    floor.data.materials.append(mat)
    bpy.ops.object.camera_add(location=(-2.65, 1.8, 1.55))
    camera = bpy.context.object
    camera.name = "Preview_Camera"
    camera.data.lens = 58
    look_at(camera, (0, 0, 0.58))
    move(camera)
    bpy.context.scene.camera = camera
    for name, location, energy, size, color in (
        ("Key", (-2.2, -3.0, 4.0), 550, 2.4, (1.0, 0.91, 0.79)),
        ("Fill", (3.0, -1.0, 2.0), 360, 2.0, (0.72, 0.86, 1.0)),
        ("Rim", (0.5, 2.2, 3.0), 650, 1.8, (0.9, 0.96, 1.0)),
    ):
        bpy.ops.object.light_add(type="AREA", location=location)
        light = bpy.context.object
        light.name = "Preview_" + name
        light.data.energy = energy
        light.data.shape = "DISK"
        light.data.size = size
        light.data.color = color
        look_at(light, (0, 0, 0.5))
        move(light)
    world = bpy.data.worlds.new("Preview_World")
    world.use_nodes = True
    world.node_tree.nodes.clear()
    background = world.node_tree.nodes.new("ShaderNodeBackground")
    world_output = world.node_tree.nodes.new("ShaderNodeOutputWorld")
    world.node_tree.links.new(background.outputs[0], world_output.inputs["Surface"])
    background.inputs["Color"].default_value = (0.18, 0.22, 0.28, 1)
    background.inputs["Strength"].default_value = 0.3
    bpy.context.scene.world = world


def prepare_render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    scene.cycles.seed = 0
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1


def render_references(directory, images):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    mesh = bpy.data.objects[MESH_NAME]
    original = mesh.data.materials[0]
    scene = bpy.context.scene
    prepare_render()
    scene.render.filepath = str(directory / "ShaderBall_PBR_Blender.png")
    bpy.ops.render.render(write_still=True)
    preview = build_material(images, "albedo-preview")
    try:
        mesh.data.materials[0] = preview
        # An emission shader reproduces the next sample's unlit Albedo display.
        scene.view_settings.view_transform = "Standard"
        scene.view_settings.look = "None"
        scene.render.filepath = str(directory / "ShaderBall_Albedo_Blender.png")
        bpy.ops.render.render(write_still=True)
    finally:
        mesh.data.materials[0] = original
        bpy.data.materials.remove(preview)
        prepare_render()
