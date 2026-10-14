"""New-source generation only; never run to export an edited source. MIT License."""
import argparse
from pathlib import Path
import sys
import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import HERE, arguments, material, create_rig, model_geometry, create_actions, studio

p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=HERE)
args=arguments(p);out=args.source_dir.resolve();out.mkdir(parents=True,exist_ok=True)
target=out/'SilverRobot.blend'
if target.exists():raise RuntimeError('Source exists. Choose a new --source-dir; preserve saved edits.')
bpy.ops.wm.read_factory_settings(use_empty=True)
export=bpy.data.collections.new('EXPORT');bpy.context.scene.collection.children.link(export)
preview=bpy.data.collections.new('PREVIEW_ONLY');bpy.context.scene.collection.children.link(preview)
material('PearlSilver',(.68,.72,.76),.38,.36)
material('JointGraphite',(.030,.036,.045),.42,.42)
material('BrushedTitanium',(.17,.20,.235),.80,.32)
material('InsetBlack',(.014,.020,.028),.28,.40)
material('VisorCyan',(.015,.55,.67),.25,.23,(.015,.65,.85))
rig=create_rig(export);obj=model_geometry(rig);create_actions(rig);studio(preview)
preview.hide_viewport=True
scene=bpy.context.scene;scene.frame_start=1;scene.frame_end=31;scene.frame_set(1)
scene['asset_version']='SilverRobot 1.0'
scene['license']='MIT License, Copyright (c) 2026 masafumi'
scene['reference']='User supplied silver humanoid turnaround, used as visual reference; reference image is not embedded or redistributed.'
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
rig.show_in_front=False
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.shading.type='MATERIAL';space.shading.studiolight_rotate_z=.35
            space.overlay.show_floor=False;space.overlay.show_axis_x=False;space.overlay.show_axis_y=False
            space.region_3d.view_distance=3.5;space.region_3d.view_location=Vector((0,0,1.0))
            space.region_3d.view_rotation=Quaternion((1,0,0),1.570796)
            space.region_3d.view_perspective='ORTHO'
bpy.ops.wm.save_as_mainfile(filepath=str(target))
print('Saved '+str(target),flush=True)
