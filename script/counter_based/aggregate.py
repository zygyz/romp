# aggregate.py

import argparse
import json
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

def write_results(output_path: str, results: list) -> None:
  if len(results) == 0:
    return
  output_csv_file = os.path.join(output_path, 'result.csv');
  with open(output_csv_file, 'w') as f:
    writer = csv.DictWriter(f, fieldnames=results[0].keys())
    writer.writeheader()
    writer.writerows(results) 
 
def aggregate_data_for_metric(metric_name: str, data: dict, baseline_branch: str, compare_branch: str) -> dict:
  result = {};
  compare_ratio_tag = compare_branch + '/' + baseline_branch;
  for benchmark, metrics in data.items():
    for metric, values in metrics.items():
      if metric == metric_name:
        result[benchmark] = values.get(compare_ratio_tag); 
        break;    
  return {key:value for key, value in result.items() if value > 0};

def aggregate_performance_result(data: dict, baseline_branch: str, compare_branch: str) -> dict:
  aggregated_results = {}
  for metric in metrics_config.metrics_key_list_for_visualization:
    result = aggregate_data_for_metric(metric, data, baseline_branch, compare_branch);
    aggregated_results[metric] = result;
  return aggregated_results;

def calculate_statistics_for_aggregated_results(results: dict) -> None:
  for metric in metrics_config.metrics_key_list_for_visualization:
    result = results[metric];
    values = result.values();
    print('Metric: ', metric);
    print('Mean: ', sum(values) / len(values));

def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance visualizer');
  parser.add_argument('performance_profile_path', type=str, help="path to performance profile json file");
  parser.add_argument('baseline_branch', type=str, help="baseline branch name"); 
  parser.add_argument('compare_branch', type=str, help="compare branch name");
  parser.add_argument('-p', '--pprint', action="store_true", help="parse the json file and pretty print to stdout");
  args = parser.parse_args();
  data = parse_performance_profile(args.performance_profile_path); 
  if data is None:
    reutrn -1;
  if (args.pprint):
    pprint.PrettyPrinter(indent=2).pprint(data);
  aggregated_results = aggregate_performance_result(data, args.baseline_branch, args.compare_branch);
  calculate_statistics_for_aggregated_results(aggregated_results);
  return 0;

if __name__ == '__main__':
  sys.exit(main());
