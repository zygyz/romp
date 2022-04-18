# dynamic.py
# apply schedule(dynamic, 1) to source code 
import argparse
import os
import sys

def set_dynamic(file_name: str) -> None:
  output_lines = [];
  with open(file_name) as file:
    lines = file.readlines();
    for line in lines:
      amended_line = line;
      target_terms = ['#pragma omp parallel for', '#pragma omp for'];
      for target_term in target_terms:
        if target_term in line:
          if 'schedule(static,1)' in line:
            amended_line = line.replace('schedule(static,1)', 'schedule(dynamic,1)');
          elif 'schedule(static, 1)' in line:
            amended_line = line.replace('schedule(static, 1)', 'schedule(dynamic,1)');
          elif 'schedule(' not in line:
            desire_term = target_term + ' schedule(dynamic,1)';
            amended_line = line.replace(target_term, desire_term);
      output_lines.append(amended_line.rstrip());
  with open(file_name, 'w') as file: # overwrite the original file
    for line in output_lines:
      file.write("%s\n" % line);

def batch_set_dynamic(directory: str) -> None:
  print(directory);
  list_of_files = sorted([f for f in os.listdir(directory) if f.endswith('.c') or f.endswith('.cpp')]);
  for f in list_of_files:
    set_dynamic(f); 
  return;

def main() -> int:
  parser = argparse.ArgumentParser(description='Argument parsing for dynamic clause code modifier');
  parser.add_argument('directory', type=str, help="directory where batch edit happens");
  args = parser.parse_args();
  batch_set_dynamic(args.directory);

if __name__ == '__main__':
  sys.exit(main()); 
