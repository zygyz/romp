# visualize.py

import argparse
import json
import matplotlib.pyplot as plt
import os
import pprint
import sys

import metrics_config

def parse_performance_profile(profile_path: str) -> dict:
  try: 
    with open(profile_path) as f:
      return json.load(f);
  except:
    print('failed to open profile file: ', profile_path);
    return {}
 
def aggregate_data_for_metric(metric_name: str, data: dict, baseline_branch: str, compare_branch: str) -> dict:
  result = {};
  compare_ratio_tag = compare_branch + '/' + baseline_branch;
  for benchmark, metrics in data.items():
    for metric, values in metrics.items():
      if metric == metric_name:
        result[benchmark] = values.get(compare_ratio_tag); 
        break;    
  return {key:value for key, value in result.items() if value > 0};

def plot_performance_result(data: dict, baseline_branch: str, compare_branch: str) -> None:
  for metric in metrics_config.metrics_key_list_for_visualization:
    result = aggregate_data_for_metric(metric, data, baseline_branch, compare_branch);
    draw(metric, result);

def draw(metric_name: str, data: dict) -> None: 
  values = list(data.values());
  fig, ax = plt.subplots();
  ax.axes.xaxis.set_visible(False);
  ax.set_title(metric_name); 
  ax.plot(list(range(0, len(values))), values, "*");
  fig.savefig(metric_name + '.png');
  return;

def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance visualizer');
  parser.add_argument('performance_profile_path', type=str, help="path to performance profile json file");
  parser.add_argument('baseline_branch', type=str, help="baseline branch name"); 
  parser.add_argument('compare_branch', type=str, help="compare branch name");
  parser.add_argument('-p', '--pprint', action="store_true", help="parse the json file and pretty print to stdout");
  parser.add_argument('-d', '--draw', action="store_true", help="parse the json file and plot the data");
  args = parser.parse_args();
  data = parse_performance_profile(args.performance_profile_path); 
  if data is None:
    reutrn -1;
  if (args.pprint):
    pprint.PrettyPrinter(indent=2).pprint(data);
  if (args.draw):
    plot_performance_result(data, args.baseline_branch, args.compare_branch);
  return 0;

if __name__ == '__main__':
  sys.exit(main());
