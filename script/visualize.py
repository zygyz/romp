# visualize.py

import argparse
import json
import os
import pprint
import sys


def parse_performance_profile(profile_path: str) -> None:
  try: 
    with open(profile_path) as f:
  except:
    print('failed to open profile file: ', profile_path);
    return;

def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance visualizer');
  parser.add_argument('performance_profile_path', type=str, help="path to performance profile json file");
  parser.add_argument('-p', '--pprint', action="store_true", help="parse the json file and pretty print to stdout");
  args = parser.parse_args();
  if (args.pprint):
    return 
  return 0;
