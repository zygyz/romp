# metrics,py

KEY_NUM_CHECK_ACCESS_CALL="key_num_check_access_call";
KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL="key_num_memory_access_instrumentation_call";
KEY_NUM_ACCESS_CONTROL_CONTENTION="key_num_access_control_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION="key_num_access_control_write_write_contention";
KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION="key_num_access_control_write_read_contention";
KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION="key_num_access_control_read_write_contention";
KEY_NUM_ACCESS_HISTORY_SKIP_REMOVE_RECORDS="key_num_access_history_skip_remove_records";
KEY_AVERAGE_NUM_ACCESS_RECORDS_TRAVERSED="key_average_num_access_records_traversed";
KEY_MAX_ACCESS_RECORDS_SIZE="key_max_access_records_size";
KEY_NUM_ACCESS_HISTORY_REMOVE_RECORDS="key_num_access_history_remove_records";
KEY_NUM_SKIP_ADD_CURRENT_RECORD="key_num_skip_add_current_record";

metrics_key_name_map = {
  KEY_NUM_CHECK_ACCESS_CALL: 'Check Access Function Call',
  KEY_NUM_MEMORY_ACCESS_INSTRUMENTATION_CALL: 'Memory Access Instrumentation Call',
  KEY_NUM_ACCESS_CONTROL_CONTENTION: 'Access Control Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_WRITE_CONTENTION: 'Access Control Write Write Contention',
  KEY_NUM_ACCESS_CONTROL_WRITE_READ_CONTENTION: 'Access Control Write Read Contention',
  KEY_NUM_ACCESS_CONTROL_READ_WRITE_CONTENTION: 'Access Control Read Write Contention', 
  KEY_NUM_ACCESS_HISTORY_SKIP_REMOVE_RECORDS: 'Access History Skip Remove Records',
  KEY_AVERAGE_NUM_ACCESS_RECORDS_TRAVERSED: 'Average number access records traversed',
  KEY_MAX_ACCESS_RECORDS_SIZE: "Maximum Access Records Number",
  KEY_NUM_ACCESS_HISTORY_REMOVE_RECORDS: 'Access History Remove Records',
  KEY_NUM_SKIP_ADD_CURRENT_RECORD: 'Skip Add Current Record',
}

metrics_key_list_for_visualization  = [KEY_NUM_CHECK_ACCESS_CALL, KEY_NUM_ACCESS_CONTROL_CONTENTION, KEY_AVERAGE_NUM_ACCESS_RECORDS_TRAVERSED, KEY_MAX_ACCESS_RECORDS_SIZE];
