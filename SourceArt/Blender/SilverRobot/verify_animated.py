"""Numerically compare saved retargeted actions and actual exported CPU skinning."""
from pathlib import Path
import sys
import hashlib
import json
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import ROOT,write_json
from validate import read,matrices,blender_compare
out=ROOT/'Assets/Models/SilverRobotAnimated';path=out/'SilverRobot.gltf'
doc,a=read(path)
assert len(doc['skins'])==1 and len(doc['skins'][0]['joints'])==53
names={x['name'] for x in doc['animations']}
assert names=={'Bind_Apose','Retarget_Tpose','UAL_Idle','UAL_Walk','UAL_Run','UAL_JumpStart','UAL_JumpLoop','UAL_JumpLand'}
report={'blender':bpy.app.version_string,'buildHash':bpy.app.build_hash.decode(),'cpuSkinning':blender_compare(path)}
report['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.suffix in ('.gltf','.glb','.bin','.json','.txt')}
report['sourceSha256']=hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
write_json(ROOT/'Docs/Assets/SilverRobotAnimatedValidation.json',report)
print('Saved Blender vs glTF comparison: PASS',flush=True)
