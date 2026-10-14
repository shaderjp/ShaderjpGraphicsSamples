"""Export only saved retargeted geometry, skin and actions; no regeneration. MIT code."""
import argparse
import json
from pathlib import Path
import sys
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import ROOT,export_assets,write_json
if Path(bpy.data.filepath).name!='SilverRobotAnimated.blend':raise RuntimeError('Open SilverRobotAnimated.blend')
arguments=argparse.ArgumentParser()
arguments.add_argument('--output-dir',type=Path,default=ROOT/'Assets/Models/SilverRobotAnimated')
out=arguments.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []).output_dir
export_assets(out)
provenance=json.loads(bpy.context.scene['animation_provenance'])
write_json(out/'Animations.json',provenance)
(out/'Quaternius-CC0.txt').write_bytes((ROOT/'ThirdParty/UniversalAnimationLibrary/LICENSE').read_bytes())
