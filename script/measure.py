# measure.py

import argparse
import csv
import json
import os
import pprint
import shutil
import sys
import subprocess
from os import listdir
from datetime import datetime

import benchmark_config

def get_output_directory_path(benchmark_root_path: str, branch: str) -> str:
  return os.path.join(benchmark_root_path, 'output-'+ branch);

def create_output_directory(benchmark_root_path: str, branch: str) -> str: 
  output_path = get_output_directory_path(benchmark_root_path, branch);
  if not os.path.exists(output_path):
    os.mkdir(output_path);
  else:
    shutil.rmtree(output_path) 
    os.mkdir(output_path);
  print("Create output path: ", output_path);
  return output_path;
  
def build_romp(romp_root_path: str, branch: str) -> None:
  print("Build romp on branch ", branch);
  cwd = os.getcwd();
  os.chdir(romp_root_path);
  try: 
    os.system('git checkout ' + branch);
    os.system('./install.sh release');
  finally:
    os.chdir(cwd);

def run_benchmark(output_path: str, binary_path:str, binary_name: str, parameter: str, iteration: int, timeout: int) -> int:
  output_file_path = os.path.join(output_path, binary_name + "_" + str(iteration) + ".out");
  run_string = assemble_run_string(binary_path, parameter, timeout);
  print('run: ', run_string);
  return os.system(run_string + ' &> ' + output_file_path); 

def assemble_run_string(binary_path: str, parameter: str, timeout: int) -> str:
  return "timeout " + str(timeout) + "m /usr/bin/time -f 'Memory Usage: %M Time: %E' " + binary_path + " " + parameter;

def process_output_files(output_path: str) -> dict:  
  output_files = [f for f in listdir(output_path) if os.path.isfile(os.path.join(output_path, f))]
  print(output_files)
  result = {};
  if os.stat(output_file_path).st_size == 0:
    return result;
  with open(output_file_path) as file:
    lines = file.readlines();
    print(lines)
  return result;

#calculate the average time and memory overhead
def calculate_metrics(output_path: str, benchmark: str) -> dict:
  output_files = [os.path.join(output_path, f) for f in listdir(output_path) if benchmark in f]
  original_memory_list = [];
  instrument_memory_list = [];
  original_time_list = [];
  instrument_time_list = [];

  for output_file in output_files:
    is_instrument_output = '.inst' in output_file 
    with open(output_file) as file:
      lines = file.readlines(); 
      for line in lines:
        if 'Memory Usage' in line:
          result = line.split()
          time = datetime.strptime(result[-1], "%M:%S.%f")
          time_in_milliseconds = time.minute * 60 * 1000 + time.second * 1000 + time.microsecond / 1000;
          memory_in_kb = int(result[-3]);
          if is_instrument_output:
            instrument_time_list.append(time_in_milliseconds);   
            instrument_memory_list.append(memory_in_kb);
          else:
            original_time_list.append(time_in_milliseconds);
            original_memory_list.append(memory_in_kb);
  
  original_memory_list.sort();
  original_time_list.sort();
  instrument_memory_list.sort();
  instrument_time_list.sort();
  
  if len(original_memory_list) > 3:
    original_memory_list = original_memory_list[1:-1];
  if len(original_time_list) > 3:
    original_time_list = original_time_list[1:-1];
  if len(instrument_memory_list) > 3:
    instrument_memory_list = instrument_memory_list[1:-1];
  if len(instrument_time_list) > 3:
    instrument_time_list = instrument_time_list[1:-1];

  original_time_average = sum(original_time_list) / len(original_time_list)
  instrument_time_average = sum(instrument_time_list) / len(instrument_time_list);
  original_memory_average = sum(original_memory_list) / len(original_memory_list);
  instrument_memory_average = sum(instrument_memory_list) / len(instrument_memory_list);

  result = {
               'benchmark' : benchmark,
               'original_memory' : original_memory_average,
               'original_time' : original_time_average,
               'instrument_memory' : instrument_memory_average, 
               'instrument_time' : instrument_time_average,
               'memory_overhead' : instrument_memory_average / original_memory_average, 
               'time_overhead' : instrument_time_average / original_time_average, 
             }
  return result
    


def run(benchmark_root_path: str, output_path: str, branch: str, iteration: int, timeout: int) -> list:
  print('run benchmarks');
  results = []
  for benchmark, parameter in benchmark_config.benchmark_parameter_map.items():
    print('running benchmark: ' + benchmark + ", with parameter: " + parameter)
    original_binary_name = benchmark
    instrument_binary_name = original_binary_name + ".inst" 

    original_binary_path = os.path.join(benchmark_root_path, original_binary_name);
    instrument_binary_path = os.path.join(benchmark_root_path, instrument_binary_name);
    
    not_success_finish = False; 
    for i in range(0, iteration):  
      print('running iteration : ', i);
      ret_val_original = run_benchmark(output_path, original_binary_path, original_binary_name, parameter, i, timeout);
      ret_val_instrument = run_benchmark(output_path, instrument_binary_path, instrument_binary_name, parameter, i, timeout);
      print('origin return value: ', ret_val_original, ' instrument return value: ', ret_val_instrument)
      if ret_val_original != 0 or ret_val_instrument != 0:
        not_success_finish = True
        break
    if not_success_finish:
      continue;
    result = calculate_metrics(output_path, benchmark);
    result['branch'] = branch
    results.append(result) 
  return results  
    
def write_results(output_path: str, results: list) -> None:
  if len(results) == 0:
    return
  output_csv_file = os.path.join(output_path, 'result.csv');
  with open(output_csv_file, 'w') as f:
    writer = csv.DictWriter(f, fieldnames=results[0].keys())
    writer.writeheader()
    writer.writerows(results) 

def run_benchmarks_for_branch(romp_root_path: str, benchmark_root_path: str, branch: str, iteration: int, timeout: int) -> None:
  build_romp(romp_root_path, branch);
  output_path = create_output_directory(benchmark_root_path, branch);
  results = run(benchmark_root_path, output_path, branch, iteration, timeout);
  write_results(output_path, results);

  
def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for performance measurer');
  parser.add_argument('benchmark_root_path', type=str, help="root path to benchmark binaries");
  parser.add_argument('romp_root_path', type=str, help='root path to romp source');
  parser.add_argument('branch', type=str, help='branch name');
  parser.add_argument('timeout', type=int, help="time out in minute");
  parser.add_argument('iteration', type=int, help="num iterations");
  args = parser.parse_args();
  run_benchmarks_for_branch(args.romp_root_path, args.benchmark_root_path, args.branch, args.iteration, args.timeout);
  return 0;


if __name__ == '__main__':
  sys.exit(main());
