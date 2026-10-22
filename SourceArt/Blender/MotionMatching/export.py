"""Export SAVED motions without saving or regenerating the Blender source."""
import bpy
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
from mathutils import Matrix,Vector

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'SilverRobot'))
from robot_tools import export_assets,write_json

if Path(bpy.data.filepath).name!='SilverRobotMotionSet.blend':raise RuntimeError('Open the saved SilverRobotMotionSet.blend')
out=ROOT/'Assets/Models/SilverRobotMotionSet'
parser=argparse.ArgumentParser();parser.add_argument('output',nargs='?',type=Path,default=out);parser.add_argument('--verify',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []);out=args.output.resolve()
rig=bpy.data.objects['SilverRobotRig'];scene=bpy.context.scene
manifest=json.loads(scene['motion_manifest']);clips=[]
for action in list(bpy.data.actions):
    if 'motion_info' not in action:continue
    info=json.loads(action['motion_info']);roots=json.loads(action['root_track'])
    rig.animation_data.action=action
    # Root motion is authored on the rig, and removed only from exported poses.
    # A user can edit both poses and the Root bone in the saved source.
    baked=[];contacts=[];track=[];world_feet=[]
    initial=None
    for f in range(info['frames']):
        scene.frame_set(f+1)
        m=rig.pose.bones['Root'].matrix.copy();yaw=m.to_euler('XYZ').z
        if track:yaw=-track[-1][3]+math.remainder(yaw+track[-1][3],2*math.pi)
        p=m.translation.copy()
        if info['kind']=='locomotion' or info['name'] in ['MM_JumpStart','MM_JumpLoop','MM_JumpLand']:p.z=0
        if initial is None:initial=p.copy()
        p-=initial
        actor=Matrix.Translation(p)@Matrix.Rotation(yaw,4,'Z')
        track.append([p.x,p.z,p.y,-yaw]) # Reflection to LH also reverses yaw.
        target={b.name:actor.inverted()@b.matrix.copy() for b in rig.pose.bones}
        baked.append(target['Root'])
        contacts.append([bool(target['Foot.'+s].translation.z<.16) for s in ['L','R']])
        world_feet.append([rig.pose.bones['Foot.'+s].matrix.translation.copy() for s in ['L','R']])
    for f in range(info['frames']):
        a=max(0,f-1);b=min(info['frames']-1,f+1)
        for side in range(2):
            velocity=(world_feet[b][side]-world_feet[a][side])*30/(b-a)
            contacts[f][side] &= velocity.length<.65
    if info['loop']:contacts[-1]=contacts[0].copy()
    # Re-key only Root; child local transforms are invariant to extracted motion.
    for f,m in enumerate(baked,1):
        rig.pose.bones['Root'].matrix_basis=rig.data.bones['Root'].matrix_local.inverted()@m
        b=rig.pose.bones['Root'];b.keyframe_insert('location',frame=f,group='Root');b.keyframe_insert('rotation_quaternion',frame=f,group='Root')
    info['root']=track;info['contacts']=contacts
    info['spatial']=False
    clips.append(info)
export_assets(out)
manifest['clips']=clips
manifest['modelSha256']=hashlib.sha256((out/'SilverRobot.gltf').read_bytes()).hexdigest()
manifest['binarySha256']=hashlib.sha256((out/'SilverRobot.bin').read_bytes()).hexdigest()
manifest['sourceBlendSha256']=hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
write_json(out/'MotionSet.json',manifest)
(out/'Quaternius-CC0.txt').write_bytes((HERE/'Quaternius-CC0.txt').read_bytes())
print('Exported saved motion source:',len(clips),'clips',flush=True)
if args.verify:
    from validate import blender_compare
    report=blender_compare(out/'SilverRobot.gltf')
    write_json(ROOT/'.cache/motion-matching/deformation.json',dict(blender=bpy.app.version_string,clips=report))
    print('Independent glTF skinning vs Blender deformation: PASS',flush=True)
