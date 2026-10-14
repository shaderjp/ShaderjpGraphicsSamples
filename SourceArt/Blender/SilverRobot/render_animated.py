"""Render saved retargeted actions for visual QA. Does not save authoring source."""
from pathlib import Path
import sys
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import ROOT,reset_pose
scene=bpy.context.scene;rig=bpy.data.objects['SilverRobotRig']
bpy.data.collections['PREVIEW_ONLY'].hide_viewport=False
scene.camera=bpy.data.objects['Hero'];scene.cycles.samples=48
for name,frame in [('UAL_Idle',20),('UAL_Walk',10),('UAL_Run',8),('UAL_JumpStart',20),('UAL_JumpLoop',25),('UAL_JumpLand',10)]:
    rig.animation_data.action=bpy.data.actions[name];reset_pose(rig);scene.frame_set(frame)
    scene.render.filepath=str(ROOT/'Docs/Assets/Images/SilverRobotAnimated'/(name+'.png'))
    bpy.ops.render.render(write_still=True)
