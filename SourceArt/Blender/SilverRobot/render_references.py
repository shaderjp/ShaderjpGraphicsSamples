"""Real Cycles renders of saved mesh and saved actions. Does not save the scene. MIT."""
import argparse
from pathlib import Path
import sys
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import IMAGES, arguments, reset_pose
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=IMAGES)
p.add_argument('--shot',choices=('Hero','Front','Right','Back','Walk'));p.add_argument('--samples',type=int,default=48)
args=arguments(p);args.output_dir.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene;rig=bpy.data.objects['SilverRobotRig']
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=args.samples
scene.cycles.use_denoising=True;scene.render.resolution_x=1280;scene.render.resolution_y=720
scene.render.resolution_percentage=100
bpy.data.collections['PREVIEW_ONLY'].hide_viewport=False
for name in ('Hero','Front','Right','Back','Walk'):
    if args.shot and args.shot!=name:continue
    rig.animation_data.action=bpy.data.actions['Walk_InPlace'] if name=='Walk' else None
    reset_pose(rig);scene.frame_set(8 if name=='Walk' else 1)
    scene.camera=bpy.data.objects[name]
    scene.render.filepath=str(args.output_dir/(name+'.png'))
    bpy.ops.render.render(write_still=True)
    print('Rendered '+name,flush=True)
