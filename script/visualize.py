# visualize.py

import argparse
import json
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
  print(result);
  return result;

def plot_performance_result(data: dict) -> None:
  for benchmark, metrics in data.items():
    print('benchmark: ', benchmark); 
    print('metrics: ', metrics);

def draw(data: dict) -> None: 
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
    print(metrics_config.KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL);
    aggregate_data_for_metric(metrics_config.KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL, data);
#    plot_performance_result(data);
  return 0;

if __name__ == '__main__':
  sys.exit(main());
