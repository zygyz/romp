# metrics,py

BASELINE_TAG='baseline';
OPTIMIZE_TAG='optimized';
RATIO_TAG='optimized/baseline';

KEY_NUM_CHECK_ACCESS_CALL="key_num_check_access_call";
KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL="key_num_memory_access_instrumentation_call";
KEY_NUM_ACCESS_CONTROL_CONTENTION="key_num_access_control_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION="key_num_access_control_write_write_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION="key_num_access_control_write_read_contention";
KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION="key_num_access_control_read_write_contention";

metrics_key_name_map = {
  KEY_NUM_CHECK_ACCESS_CALL: 'Check Access Function Call',
  KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL: 'Memory Access Instrumentation Call',
  KEY_NUM_ACCESS_CONTROL_CONTENTION: 'Access Control Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION: 'Access Control Write Write Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION: 'Access Control Write Read Contention',
  KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION: 'Access Control Read Write Contention', 
}
