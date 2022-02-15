# performance.py

import argparse
import json
import os
import pprint
import sys

benchmark_relative_path='dataracebench/micro-benchmarks';
skipped_benchmark_list = ['008', '024', '025', '137', '138', '031', '037', '038', '041', '042', 
'043', '044', '055', '056', '058', '062', '065', '180', '070', '105', '095', '096', '097', '098',
'116','129','130','144','145','146','147','148','149','150','151','152','153','154','156','157',
'160','161','162','163','164', '110','114','122','127','128', '131','132','133','134','135','139',
'140','143','155','158','159','165','168','173','174','176','179','181'];

KEY_NUM_CHECK_ACCESS_CALL="key_num_check_access_call";
KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL="key_num_memory_access_instrumentation_call";
KEY_NUM_ACCESS_CONTROL_CONTENTION="key_num_access_control_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION="key_num_access_control_write_write_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION="key_num_access_control_write_read_contention";
KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION="key_num_access_control_read_write_contention";

metrics_key_name_map = {
  KEY_NUM_CHECK_ACCESS_CALL: 'Check Access Function Call',
  KEY_NUM_ACCESS_CONTROL_CONTENTION: 'Access History Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION: 'Access Control Write Write Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION: 'Access Control Write Read Contention',
  KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION: 'Access Control Read Write Contention', 
}

def get_output_directory_path(benchmark_root_path: str, branch: str) -> str:
  return os.path.join(benchmark_root_path, 'output-'+ branch);

def create_output_directory(benchmark_root_path: str, branch: str) -> str: 
  output_path = get_output_directory_path(benchmark_root_path, branch);
  if not os.path.exists(output_path):
    os.mkdir(output_path);
  print("Create output path: ", output_path);
  return output_path;
  
def build_romp(romp_root_path: str, branch: str) -> None:
  print("Build romp on branch ", branch);
  cwd = os.getcwd();
  os.chdir(romp_root_path);
  try: 
    os.system('git checkout ' + branch);
    os.system('./install.sh perf');
    os.system('git checkout master'); #switch back to master branch
  finally:
    os.chdir(cwd);

def run(benchmark_root_path: str, output_path: str) -> None:
  print('Run benchmarks');
  benchmark_path = os.path.join(benchmark_root_path, benchmark_relative_path);
  instrumented_binaries = sorted([f for f in os.listdir(benchmark_path) if 
                                  f.endswith('.inst') and f[3:6] not in skipped_benchmark_list]);
  for binary in instrumented_binaries:
    binary_path = os.path.join(benchmark_root_path, benchmark_relative_path, binary);
    output_file_path = os.path.join(output_path, binary + '.out');
    print('running benchmark: ', binary_path);
    os.system(binary_path + ' &> ' +  output_file_path);

def run_benchmarks_for_branch(romp_root_path: str, benchmark_root_path: str, branch: str) -> None:
  build_romp(romp_root_path, branch);
  output_path = create_output_directory(benchmark_root_path, branch);
  run(benchmark_root_path, output_path);

def extract_metric(metric_string: str, lines: list[str]) -> float | None:
  for line in lines:
    if metric_string in line:
      return float(line.strip().split()[-1]);
  return None;

def process_output_file(output_file_path: str) -> dict:
  result = {};
  if os.stat(output_file_path).st_size == 0:
    return result;
  with open(output_file_path) as file:
    lines = file.readlines();
    for metric_name, metric_string in metrics_key_name_map.items():
      result[metric_name] = extract_metric(metric_string, lines);
  return result;

   
def aggregate_result(baseline_result: dict, optimize_result: dict) -> dict:
  result = {}
  for metric_name in metrics_key_name_map:
    baseline_value = baseline_result.get(metric_name);
    optimize_value = optimize_result.get(metric_name);
    result[metric_name] = [baseline_value, optimize_value,  -1.0 if baseline_value == 0.0 or baseline_value is None or optimize_value is None else optimize_value / baseline_value];
  return result;

def calculate_performance(benchmark_root_path: str, baseline_branch: str, optimize_branch: str) -> None:
  baseline_output_path = get_output_directory_path(benchmark_root_path, baseline_branch);
  optimize_output_path = get_output_directory_path(benchmark_root_path, optimize_branch); 
  baseline_output_files = os.listdir(baseline_output_path);
  optimize_output_files = os.listdir(optimize_output_path);
  summary = {};
  for baseline_output_file in baseline_output_files:
    optimize_output_file_path = os.path.join(optimize_output_path, baseline_output_file);
    if not os.path.exists(optimize_output_file_path):
      print('WARNING: ', optimize_output_file_path , ' does not exist'); 
      continue;
    baseline_output_file_path = os.path.join(baseline_output_path, baseline_output_file); 
    baseline_result = process_output_file(baseline_output_file_path);
    optimize_result = process_output_file(optimize_output_file_path); 
    result = aggregate_result(baseline_result, optimize_result);     
    summary[baseline_output_file] = result;
  with open(os.path.join(benchmark_root_path, 'summary.json'), 'w') as json_file:
    json_file.write(json.dumps(summary));
  pp = pprint.PrettyPrinter(indent=2);
  pp.pprint(summary);
 
def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance profiler');
  parser.add_argument('benchmark_root_path', type=str, help="root path to benchmark");
  parser.add_argument('romp_root_path', type=str, help='root path to romp source');
  parser.add_argument('baseline_branch', type=str, help='baseline branch');
  parser.add_argument('optimize_branch', type=str, help='optimize branch');
  parser.add_argument('-c', '--calculate', action="store_true", help="calculate the performance");
  args = parser.parse_args();
  if args.calculate:
    calculate_performance(args.benchmark_root_path, args.baseline_branch, args.optimize_branch);
    return 0;
  run_benchmarks_for_branch(args.romp_root_path, args.benchmark_root_path, args.baseline_branch);
  run_benchmarks_for_branch(args.romp_root_path, args.benchmark_root_path, args.optimize_branch);
  return 0;


if __name__ == '__main__':
  sys.exit(main());
