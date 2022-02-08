# performance.py

import os
import sys

benchmark_relative_path='dataracebench/micro-benchmarks';
skipped_benchmark_list = ['008', '024', '25', '137', '138', '031', '037', '038', '041', '042', 
'043', '044', '055', '056', '058', '062', '065', '180', '070', '105', '095', '096', '097', '098',
'116','129','130','144','145','146','147','148','149','150','151','152','153','154','156','157',
'160','161','162','163','164', '110','114','122','127','128', '131','132','133','134','135','139',
'140','143','155','158','159','165','168','173','174','176','179','181'];

def create_output_directory(branch: str) -> str: 
  output_path = './output-'+ branch;
  if not os.path.exists(output_path):
    os.mkdir(output_path);
  print("Create output path: ", output_path);
  return output_path;
  
def build_romp(romp_root_path: str, branch: str) -> None:
  print("Build romp on branch ", branch);
  os.chdir(romp_root_path);
  os.system('git checkout ' + branch);
  os.system('./install.sh perf');

def run(benchmark_root_path: str, output_path: str) -> None:
  print('Run benchmarks');
  benchmark_path = os.path.join(benchmark_root_path, benchmark_relative_path);
  instrumented_binaries = sorted([f for f in os.listdir(benchmark_path) if 
                                  f.endswith('.inst') and f[3:6] not in skipped_benchmark_list]);
  for binary in instrumented_binaries[0:2]:
    binary_path = os.path.join(benchmark_root_path, benchmark_relative_path, binary);
    output_file_path = os.path.join(output_path, binary + '.out');
    os.system(binary_path + ' &> ' +  output_file_path );

def run_benchmarks_for_branch(romp_root_path: str, benchmark_root_path: str, branch: str) -> None:
  #build_romp(romp_root_path, branch);
  output_path = create_output_directory(branch);
  run(benchmark_root_path, output_path);

def main() -> int:
  if len(sys.argv) < 3:
    print("usage: python3 performance.py /path/to/dataracebench/ /path/to/romp/");
    return 1;
  benchmark_root_path = sys.argv[1];
  romp_root_path = sys.argv[2];
  run_benchmarks_for_branch(romp_root_path, benchmark_root_path, 'master');
  run_benchmarks_for_branch(romp_root_path, benchmark_root_path, 'optimize');
  return 0;


if __name__ == '__main__':
  sys.exit(main());
