"""Export saved authoring data without regenerating or saving the .blend. MIT License."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from robot_tools import ASSETS, arguments, export_assets
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=ASSETS)
export_assets(arguments(p).output_dir)
