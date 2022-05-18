# segment_mask.py
import argparse
import os
import sys

base_segment_mask_specs = {
  "SEGMENT_TYPE_MASK": [0, 1], 
  "WORK_SHARE_PLACEHOLDER_MASK": [2, 2],
  "TASKWAIT_SYNC_MASK" : [3, 3],
  "TASKGROUP_SYNC_MASK" : [4, 4],
  "SINGLE_MASK": [5, 6],
  "TASK_CREATE_MASK" : [7, 19],
  "RESERVED" : [20, 23],
  "LOOP_COUNT_MASK" : [24, 35], # 12 bits, 2^12 
  "PHASE_MASK" : [36, 39],
  "TASKWAIT_MASK" : [40, 43],
  "SPAN_MASK" : [44, 53],
  "OFFSET_MASK" : [54, 63],
}

def get_mask_with_bits(mask_size: int, shift: int) -> int:
  result = 0;
  for i in range(0, mask_size):
    result = result | 1 << i; 
  result = result << shift;
  return result;

def print_mask_bits(specs: dict) -> None:
  for key, value in specs.items():
    start_bit = value[0]
    end_bit = value[1] 
    mask = get_mask_with_bits(end_bit - start_bit + 1, start_bit);
    result = '0x{0:0{1}x}'.format(mask, 16); 
    print('#define ' + key + " " +  result);
  print('\n\n');

def main() -> int:
  print_mask_bits(base_segment_mask_specs)

if __name__ == '__main__':
  sys.exit(main()); 
