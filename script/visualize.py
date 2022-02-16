# visualize.py

import argparse
import json
import matplotlib.pyplot as plt
import os
import pprint
import sys

import metrics_config

def parse_performance_profile(profile_path: str) -> dict | None:
  try: 
    with open(profile_path) as f:
      return json.load(f);
  except:
    print('failed to open profile file: ', profile_path);
    return None;

def aggregate_data_for_metric(metric_name: str, data: dict) -> dict:
  result = {};
  for benchmark, metrics in data.items():
    for metric, values in metrics.items():
      if metric == metric_name:
        result[benchmark] = values.get(metrics_config.RATIO_TAG); 
        break;    
  return {key:value for key, value in result.items() if value > 0};

def plot_performance_result(data: dict) -> None:
  for metric in metrics_config.metrics_key_list_for_visualize:
    result = aggregate_data_for_metric(metric, data);
    draw(metric, result);

def draw(metric_name: str, data: dict) -> None: 
  print(data); 
  values = data.values();
  keys = data.keys();  
  print(keys);
  print(values);
  return; 

def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance visualizer');
  parser.add_argument('performance_profile_path', type=str, help="path to performance profile json file");
  parser.add_argument('-p', '--pprint', action="store_true", help="parse the json file and pretty print to stdout");
  parser.add_argument('-d', '--draw', action="store_true", help="parse the json file and plot the data");
  args = parser.parse_args();
  data = parse_performance_profile(args.performance_profile_path); 
  if data is None:
    reutrn -1;
  if (args.pprint):
    pprint.PrettyPrinter(indent=2).pprint(data);
    return 0;
  if (args.draw):
    plot_performance_result(data);
  return 0;

if __name__ == '__main__':
  sys.exit(main());
