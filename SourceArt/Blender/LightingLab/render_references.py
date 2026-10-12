"""Cycles reference renders from saved geometry and generated HDR. Never saves sources."""
from pathlib import Path
import math
import json
import sys
import bpy
from mathutils import Vector
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE))
sys.path.insert(0,str(HERE.parent/'ShaderBall'))
from generate import camera
from shader_ball_tools import look_at

if Path(bpy.data.filepath).stem!='LightingLab':raise RuntimeError('Open saved LightingLab.blend')
with bpy.data.libraries.load(str(HERE.parent/'ShaderBall'/'ShaderBall.blend'),link=False) as (available,loaded):
    loaded.objects=['ShaderBall']
ball=loaded.objects[0];bpy.context.scene.collection.objects.link(ball);ball.location=(-1.25,0,.25)
scene=bpy.context.scene
config=json.loads(scene['scene_json'])
p=config['camera']['position'];t=config['camera']['target']
cam=camera((p[0],-p[2],p[1]),(t[0],-t[2],t[1]));cam.data.sensor_fit='VERTICAL';cam.data.sensor_height=24
cam.data.lens=24/(2*math.tan(math.radians(config['camera']['fovDegrees'])/2))
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=128;scene.cycles.seed=0;scene.cycles.use_denoising=True
scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.color_depth='8'
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=0
scene.world=bpy.data.worlds.new('Reference Environment');scene.world.use_nodes=True
nodes=scene.world.node_tree.nodes;links=scene.world.node_tree.links
nodes.clear()
background=nodes.new('ShaderNodeBackground');world_output=nodes.new('ShaderNodeOutputWorld')
links.new(background.outputs[0],world_output.inputs['Surface'])
env=nodes.new('ShaderNodeTexEnvironment');env.image=bpy.data.images.load(str(HERE/'Studio.exr'))
links.new(env.outputs['Color'],background.inputs['Color'])
bpy.ops.object.light_add(type='SUN');sun=bpy.context.object;sun.name='Reference Directional';sun.data.angle=0
l=config['light']['direction'];direction=Vector((l[0],-l[2],l[1])).normalized();sun.rotation_euler=(-direction).to_track_quat('-Z','Y').to_euler()
sun.data.color=config['light']['color'];sun.data.energy=config['light']['intensity']
out=ROOT/'Docs'/'Assets'/'Images';out.mkdir(parents=True,exist_ok=True)
for name,environment_strength,sun_strength,bounces in [('Environment',1,0,1),('Shadow',0,3,1),('Combined',1,3,8)]:
    background.inputs['Strength'].default_value=environment_strength;sun.data.energy=sun_strength;scene.cycles.max_bounces=bounces
    scene.render.filepath=str(out/f'LightingLab_{name}_Cycles.png');bpy.ops.render.render(write_still=True)
