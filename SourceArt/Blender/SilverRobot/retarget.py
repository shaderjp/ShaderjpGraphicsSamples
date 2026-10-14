"""Bake CC0 Quaternius Standard clips onto the saved MIT robot. Never overwrite A-pose source."""
import argparse
import hashlib
import math
from pathlib import Path
import sys
import bpy
from mathutils import Matrix, Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import ROOT,HERE,arguments,reset_pose,write_json

CLIPS=[('Idle_Loop','UAL_Idle',True),('Walk_Loop','UAL_Walk',True),('Jog_Fwd_Loop','UAL_Run',True),
       ('Jump_Start','UAL_JumpStart',False),('Jump_Loop','UAL_JumpLoop',True),('Jump_Land','UAL_JumpLand',False)]
PIN='e24c23cf2a1323488a3faa226ea7ea21f644b73e'
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=HERE/'SilverRobotAnimated.blend')
    p.add_argument('--overwrite',action='store_true');args=arguments(p)
    if Path(bpy.data.filepath).name!='SilverRobot.blend':raise RuntimeError('Open saved original SilverRobot.blend')
    if args.output.resolve()==Path(bpy.data.filepath).resolve():raise RuntimeError('Preserve original A-pose source')
    if args.output.exists() and not args.overwrite:raise RuntimeError('Animated source exists; choose another --output or explicitly --overwrite')
    rig=bpy.data.objects['SilverRobotRig'];obj=bpy.data.objects['SilverRobot']
    rig.animation_data.action=bpy.data.actions['Retarget_Tpose'];bpy.context.scene.frame_set(1)
    reference={b.name:b.matrix.copy() for b in rig.pose.bones}
    rig.animation_data.action=None;reset_pose(rig)
    original_objects=set(bpy.data.objects)
    library=ROOT/'ThirdParty/UniversalAnimationLibrary/glTF/AnimationLibrary_Godot_Standard.gltf'
    bpy.ops.import_scene.gltf(filepath=str(library))
    imported=set(bpy.data.objects)-original_objects
    source=next(o for o in imported if o.type=='ARMATURE')
    for track in source.animation_data.nla_tracks:track.mute=True
    bone_map={'Root':'root','Hips':'DEF-hips','Spine':'DEF-spine.001','Spine1':'DEF-spine.002',
              'Chest':'DEF-spine.003','Neck':'DEF-neck','Head':'DEF-head'}
    for side in ('L','R'):
        for name,source_name in [('Shoulder','shoulder'),('UpperArm','upper_arm'),('ForeArm','forearm'),('Hand','hand'),
                                 ('UpperLeg','thigh'),('LowerLeg','shin'),('Foot','foot'),('Toes','toe')]:
            bone_map[name+'.'+side]='DEF-'+source_name+'.'+side
        for name,source_name in [('Thumb','thumb'),('Index','f_index'),('Middle','f_middle'),('Ring','f_ring'),('Little','f_pinky')]:
            for i in range(1,4):bone_map[name+str(i)+'.'+side]='DEF-'+source_name+'.0'+str(i)+'.'+side
    assert len(bone_map)==53 and all(n in source.data.bones for n in bone_map.values())
    source_rest={n:source.matrix_world@source.data.bones[n].matrix_local for n in bone_map.values()}
    height_ratio=rig.data.bones['Hips'].head_local.z/source_rest['DEF-hips'].translation.z
    inverse={b.name:b.matrix_local.inverted() for b in rig.data.bones}
    foot_vertices=[]
    group_names={g.index:g.name for g in obj.vertex_groups}
    for v in obj.data.vertices:
        w=[(group_names[g.group],g.weight) for g in v.groups if group_names[g.group] in rig.data.bones and g.weight>0]
        if any(n.startswith(('Foot.','Toes.')) for n,weight in w):foot_vertices.append((v.co.copy(),w))
    keep={'Bind_Apose','Retarget_Tpose'};reports=[]
    for track in list(rig.animation_data.nla_tracks):
        if track.name not in keep:rig.animation_data.nla_tracks.remove(track)
    for source_name,name,loop in CLIPS:
        src=bpy.data.actions[source_name];start,end=src.frame_range;steps=max(1,round(end-start))
        action=bpy.data.actions.new(name);action.use_fake_user=True;action.use_frame_range=True
        action.frame_start=1;action.frame_end=steps+1;action['license']='CC0-1.0; Quaternius';action['source_action']=source_name
        action['loop']=loop;source.animation_data.action=src;rig.animation_data.action=action
        first=None;max_lift=0
        for f in range(steps+1):
            at=start+(end-start)*f/steps;bpy.context.scene.frame_set(int(at),subframe=at-int(at))
            posed={n:source.matrix_world@source.pose.bones[n].matrix for n in bone_map.values()}
            target={}
            for b in rig.data.bones:
                n=b.name;sb=bone_map[n]
                base=target[b.parent.name]@inverse[b.parent.name]@b.matrix_local if b.parent else b.matrix_local.copy()
                head=base.translation.copy()
                if n=='Hips':head+=(posed[sb].translation-source_rest[sb].translation)*height_ratio
                # Use the target T-pose reference to compensate the A-pose bind arm angle.
                delta=posed[sb].to_quaternion()@source_rest[sb].to_quaternion().inverted()
                q=delta@reference[n].to_quaternion()
                target[n]=Matrix.Translation(head)@q.to_matrix().to_4x4()
            lowest=min(sum((((target[n]@inverse[n])@v)*weight for n,weight in w),Vector((0,0,0))).z for v,w in foot_vertices)
            lift=max(0,-lowest);max_lift=max(max_lift,lift)
            # Ground correction is baked into Root, not runtime procedural animation.
            for matrix in target.values():matrix.translation.z+=lift
            for b in rig.data.bones:
                basis=inverse[b.name]@b.parent.matrix_local@target[b.parent.name].inverted()@target[b.name] if b.parent else inverse[b.name]@target[b.name]
                rig.pose.bones[b.name].matrix_basis=basis
            if f==0:first={b.name:b.matrix_basis.copy() for b in rig.pose.bones}
            if loop and f==steps:
                for b in rig.pose.bones:b.matrix_basis=first[b.name]
            for b in rig.pose.bones:
                b.keyframe_insert('location',frame=f+1,group=b.name);b.keyframe_insert('rotation_quaternion',frame=f+1,group=b.name);b.keyframe_insert('scale',frame=f+1,group=b.name)
        rig.animation_data.action=action
        track=rig.animation_data.nla_tracks.new();track.name=name;track.strips.new(name,1,action);track.mute=True
        reports.append({'sourceAction':source_name,'action':name,'sourceSeconds':float((end-start)/30),
                        'bakedSeconds':steps/30,'frames':steps+1,'loop':loop,'maximumGroundLiftMetres':max_lift})
        keep.add(name);print('Baked '+name,flush=True)
    for o in imported:bpy.data.objects.remove(o,do_unlink=True)
    for action in list(bpy.data.actions):
        if action.name not in keep:bpy.data.actions.remove(action,do_unlink=True)
    rig.animation_data.action=None;reset_pose(rig);bpy.context.scene.frame_set(1)
    bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=76
    bpy.context.scene.unit_settings.system='METRIC'
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    provenance={'creator':'Quaternius','license':'CC0-1.0','official':'https://quaternius.itch.io/universal-animation-library',
                'mirrorCommit':PIN,'mirrorDistributionDate':'2025-06-10','heightRatio':height_ratio,'clips':reports,'boneMap':bone_map,
                'sourceSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [library,library.with_suffix('.bin'),ROOT/'ThirdParty/UniversalAnimationLibrary/LICENSE']}}
    bpy.context.scene['animation_provenance']=__import__('json').dumps(provenance)
    args.output.parent.mkdir(parents=True,exist_ok=True);bpy.ops.wm.save_as_mainfile(filepath=str(args.output))
    write_json(HERE/'Retargeting.json',provenance)
if __name__=='__main__':main()
