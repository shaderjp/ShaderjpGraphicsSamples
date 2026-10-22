"""Create a NEW motion source from the saved A-pose robot and pinned CC0 packs.
Never run this for ordinary edits: export.py exports the saved edited source.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
from mathutils import Matrix, Vector, Quaternion

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'SilverRobot'))
from robot_tools import reset_pose, write_json

def main():
    output = HERE / 'SilverRobotMotionSet.blend'
    if output.exists():
        raise RuntimeError('Source already exists; edit it and run export.py')
    bpy.ops.wm.open_mainfile(filepath=str(HERE.parent/'SilverRobot/SilverRobot.blend'))
    scene = bpy.context.scene
    scene.render.fps = 30
    rig = bpy.data.objects['SilverRobotRig']
    mesh = bpy.data.objects['SilverRobot']
    rig.animation_data.action = bpy.data.actions['Retarget_Tpose']
    scene.frame_set(1)
    reference = {b.name: b.matrix.copy() for b in rig.pose.bones}
    rig.animation_data.action = None
    for t in list(rig.animation_data.nla_tracks): rig.animation_data.nla_tracks.remove(t)
    for a in list(bpy.data.actions): bpy.data.actions.remove(a, do_unlink=True)
    reset_pose(rig)
    mapping = dict(Root='root', Hips='pelvis', Spine='spine_01', Spine1='spine_02', Chest='spine_03', Neck='neck_01', Head='Head')
    for side in ['L','R']:
        for a,b in [('Shoulder','clavicle'),('UpperArm','upperarm'),('ForeArm','lowerarm'),('Hand','hand'),('UpperLeg','thigh'),('LowerLeg','calf'),('Foot','foot'),('Toes','ball')]: mapping[a+'.'+side]=b+'_'+side.lower()
        for a,b in [('Thumb','thumb'),('Index','index'),('Middle','middle'),('Ring','ring'),('Little','pinky')]:
            for n in range(1,4): mapping[a+str(n)+'.'+side]=f'{b}_{n:02}_{side.lower()}'
    inverse = {b.name:b.matrix_local.inverted() for b in rig.data.bones}
    reports=[]; inventory=[]
    def save_action(name, poses, roots, loop, source, kind='locomotion'):
        action=bpy.data.actions.new(name);action.use_fake_user=True
        action.use_frame_range=True;action.frame_start=1;action.frame_end=len(poses)
        rig.animation_data.action=action
        for f,(pose,root) in enumerate(zip(poses,roots),1):
            actor=Matrix.Translation(Vector(root[:3]))@Matrix.Rotation(root[3],4,'Z')
            target={n:actor@m for n,m in pose.items()}
            for b in rig.data.bones:
                rig.pose.bones[b.name].matrix_basis=(inverse[b.name]@b.parent.matrix_local@target[b.parent.name].inverted()@target[b.name]) if b.parent else inverse[b.name]@target[b.name]
            for b in rig.pose.bones:
                b.keyframe_insert('location',frame=f,group=b.name);b.keyframe_insert('rotation_quaternion',frame=f,group=b.name);b.keyframe_insert('scale',frame=f,group=b.name)
        info=dict(name=name,loop=loop,kind=kind,source=source,frames=len(poses),duration=(len(poses)-1)/30)
        action['motion_info']=json.dumps(info)
        action['root_track']=json.dumps(roots)
        reports.append(info)
        print('Baked',name,len(poses),flush=True)
        return action
    def sample_source(source, action):
        source.animation_data.action=action
        rest={n:source.matrix_world@source.data.bones[n].matrix_local for n in mapping.values()}
        ratio=rig.data.bones['Hips'].head_local.z/rest['pelvis'].translation.z
        start,end=action.frame_range;count=max(1,round(end-start));poses=[];roots=[]
        for f in range(count+1):
            at=start+(end-start)*f/count;scene.frame_set(int(at),subframe=at-int(at))
            root=source.matrix_world@source.pose.bones['root'].matrix
            delta=root@rest['root'].inverted();inv=delta.inverted()
            posed={n:inv@source.matrix_world@source.pose.bones[n].matrix for n in mapping.values()}
            target={}
            for b in rig.data.bones:
                n=b.name;sb=mapping[n]
                base=target[b.parent.name]@inverse[b.parent.name]@b.matrix_local if b.parent else b.matrix_local.copy()
                p=base.translation.copy()
                if n=='Hips':p+=(posed[sb].translation-rest[sb].translation)*ratio
                q=(posed[sb].to_quaternion()@rest[sb].to_quaternion().inverted())@reference[n].to_quaternion()
                target[n]=Matrix.Translation(p)@q.to_matrix().to_4x4()
            # Keep an ankle-to-sole offset; full mesh validation is performed after export.
            lowest=min(target['Foot.'+s].translation.z-.105 for s in ['L','R'])
            lift=max(0,-lowest)
            for m in target.values():m.translation.z+=lift
            poses.append(target)
            roots.append([float(delta.translation.x*ratio),float(delta.translation.y*ratio),float(delta.translation.z*ratio),float(delta.to_euler('XYZ').z)])
        return poses,roots
    saved={}
    for pack in [1]:
        path=next((ROOT/f'.cache/motion-matching/UAL{pack}').rglob(f'UAL{pack}_Standard_RM.glb'))
        before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(path))
        imported=set(bpy.data.objects)-before;source=next(o for o in imported if o.type=='ARMATURE')
        actions={t.strips[0].action.name.split('.')[0]:t.strips[0].action for t in source.animation_data.nla_tracks}
        for t in source.animation_data.nla_tracks:t.mute=True
        inventory.append(dict(pack=pack,url='https://quaternius.itch.io/universal-animation-library'+('-2' if pack==2 else ''),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),archiveSha256=hashlib.sha256((ROOT/f'.cache/motion-matching/UAL{pack}-Standard.zip').read_bytes()).hexdigest(),actions=sorted(actions)))
        clips={'Idle_Loop':'Idle','Walk_Loop':'Walk','Jog_Fwd_Loop':'Run','Sprint_Loop':'Sprint','Jump_Start':'JumpStart','Jump_Loop':'JumpLoop','Jump_Land':'JumpLand'}
        for src,name in clips.items():
            poses,roots=sample_source(source,actions[src]);loop=name in ['Idle','Walk','Run','Sprint','JumpLoop']
            if loop:
                poses[-1]={n:m.copy() for n,m in poses[0].items()}
            # Match straight locomotion root speed to the planted foot instead of
            # trusting the pack's synthetic displacement (Jog is ~5 m per cycle).
            if name in ['Walk','Run','Sprint']:
                speeds=[]
                for a,b in zip(poses,poses[1:]):
                    for s in ['L','R']:
                        p=a['Foot.'+s].translation;q=b['Foot.'+s].translation
                        if p.z<.17 and q.y-p.y>.005:speeds.append((q.y-p.y)*30)
                speed=statistics.median(speeds) if speeds else {'Walk':1.5,'Run':3.,'Sprint':4.5}[name]
                roots=[[0,-speed*f/30,0,0] for f in range(len(poses))]
            save_action('MM_'+name,poses,roots,loop,f'UAL{pack}:{src}', 'locomotion' if name in ['Idle','Walk','Run','Sprint'] else 'action')
            saved[name]=(poses,roots)
        for o in imported:bpy.data.objects.remove(o,do_unlink=True)
        for a in actions.values():
            if a.name in bpy.data.actions:bpy.data.actions.remove(a,do_unlink=True)
    # Authored curved walks, starts and stops fill gaps in the free libraries.
    poses,root=saved['Walk'];speed=-root[-1][1]/((len(root)-1)/30)
    for angle in [-180,-90,90,180]:
        r=[];p=Vector((0,0,0))
        for f in range(len(poses)):
            yaw=math.radians(angle)*f/(len(poses)-1)
            if f:p+=Vector((math.sin(yaw),-math.cos(yaw),0))*speed/30
            r.append([*p,yaw])
        save_action('MM_Turn'+str(angle).replace('-','Left'),poses,r,False,'Blender authored curved Walk', 'locomotion')
    idle=saved['Idle'][0][0]
    for stopping in [False,True]:
        frames=31;ps=[];rs=[];pos=Vector((0,0,0));phase=0.
        for f in range(frames):
            u=f/(frames-1);w=(1-u) if stopping else u;w=w*w*(3-2*w)
            phase+=w
            walk=poses[int(phase)%(len(poses)-1)]
            ps.append({n:idle[n].lerp(walk[n],w) for n in idle})
            if f:pos.y-=speed*w/30
            rs.append([*pos,0])
        save_action('MM_Stop' if stopping else 'MM_Start',ps,rs,False,'Blender authored Idle/Walk transition')
    rig.animation_data.action=None;reset_pose(rig);scene.frame_set(1)
    scene['motion_manifest']=json.dumps(dict(version=1,fps=30,creator='Quaternius; Blender adaptations masafumi',license='CC0-1.0 motions; MIT robot and authoring code',sources=inventory,clips=reports,boneMap=mapping))
    scene.frame_start=1;scene.frame_end=91
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    write_json(HERE/'SourceInventory.json',inventory)

if __name__=='__main__':main()
