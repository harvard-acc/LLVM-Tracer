#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define RET_OP 1
#define MAX_FUNC_NAME_LEN 512

typedef enum _logging_status {
  // Log the current instruction (and its parameters) and then stop.
  LOG_AND_STOP,
  // Log the current instruction and continue logging.
  LOG_AND_CONTINUE,
  // Do not log the current instruction.
  DO_NOT_LOG,
} logging_status;

void trace_logger_init();
void trace_logger_write_labelmap(char* labelmap_buf, size_t labelmap_size);
void trace_logger_log0(int line_number, char *name, char *bbid, char *instid,
                       int opcode, bool is_tracked_function, bool is_toplevel_mode);
void trace_logger_log_label();
void trace_logger_fin();

gzFile full_trace_file;
bool initp = false;
int inst_count = 0;
char* current_toplevel_function;
logging_status current_logging_status;

void trace_logger_write_labelmap(char* labelmap_buf, size_t labelmap_size) {
    if (!initp) {
      trace_logger_init();
    }
    const char* section_header = "%%%% LABEL MAP START %%%%\n";
    const char* section_footer = "%%%% LABEL MAP END %%%%\n\n";
    gzwrite(full_trace_file, section_header, strlen(section_header));
    gzwrite(full_trace_file, labelmap_buf, labelmap_size);
    gzwrite(full_trace_file, section_footer, strlen(section_footer));
}

void trace_logger_init() {
  full_trace_file = gzopen("dynamic_trace.gz", "w");

  if (full_trace_file == NULL) {
    perror("Failed to open logfile \"dynamic_trace\"");
    exit(-1);
  }

  current_toplevel_function = (char*) calloc(MAX_FUNC_NAME_LEN, 1);
  current_logging_status = DO_NOT_LOG;

  atexit(&trace_logger_fin);
  initp = true;
}

void trace_logger_fin() {
  free(current_toplevel_function);
  gzclose(full_trace_file);
}

// Determine whether to log the current and next instructions.
//
// This can get a bit hairy, so here is the truth table.
//
// TLM = is_toplevel_mode
// TLF = is_toplevel_function (aka, does this function exist in the WORKLOAD
//       env variable?).
// RET = is this opcode a return?
//
// ============================================
//   TLM | TLF | RET | Behavior
// --------------------------------------------
//    0     0     0     DO_NOT_LOG
// --------------------------------------------
//    0     0     1     DO_NOT_LOG
// --------------------------------------------
//    0     1     0     LOG_AND_CONTINUE
// --------------------------------------------
//    0     1     1     LOG_AND_CONTINUE
// --------------------------------------------
//    1     0     0     Continue current status
// --------------------------------------------
//    1     0     1     Continue current status
// --------------------------------------------
//    1     1     0     LOG_AND_CONTINUE
// --------------------------------------------
//
//                      if current_function == current_toplevel_function
//    1     1     1         LOG_AND_STOP
//                      else
//                          BAD_BEHAVIOR
//
// ============================================
//
// The very last line (when all three variables are true) is to ensure that
// we don't ever call a top-level function from within another top level
// function (since this would defeat the purpose of having top level
// functions).
logging_status log_or_not(bool is_toplevel_mode, bool is_toplevel_function,
                          int opcode, char *current_function) {
  if (!is_toplevel_mode)
    return is_toplevel_function ? LOG_AND_CONTINUE : DO_NOT_LOG;

  if (!is_toplevel_function)
    return current_logging_status;

  if (opcode != RET_OP)
    return LOG_AND_CONTINUE;

  if (strlen(current_toplevel_function) == 0)
    assert(false &&
           "Returning from within a toplevel function before it was called!");

  if (strcmp(current_function, current_toplevel_function) == 0)
    return LOG_AND_STOP;

  assert(false && "Cannot call a top level function from within another one!");

  // Unreachable.
  return LOG_AND_CONTINUE;
}

// Convert @size bytes in the buffer @value to a hex string, stored in @buf.
//
// The output string is prefixed by 0x and is null terminated. The output does
// NOT account for endianness!
void convert_bytes_to_hex(char* buf, uint8_t* value, int size) {
  sprintf(buf, "0x");
  buf += 2;
  for (int i = 0; i < size; i++) {
    buf += sprintf(buf, "%02x", value[i]);
  }
  *buf = 0;
}

void update_logging_status(char *name, int opcode, bool is_tracked_function,
                           bool is_toplevel_mode) {
  // LOG_AND_STOP would have been set by the previous instruction (which should
  // be logged), and this is already the next one, so STOP.
  if (current_logging_status == LOG_AND_STOP) {
    printf("Stopping logging at inst %d.\n", inst_count);
    current_logging_status = DO_NOT_LOG;
    return;
  }

  logging_status temp = current_logging_status;

  current_logging_status =
      log_or_not(is_toplevel_mode, is_tracked_function, opcode, name);

  if (temp == DO_NOT_LOG && current_logging_status != temp)
    printf("Starting to log at inst = %d.\n", inst_count);

  if (strlen(current_toplevel_function) == 0 &&
      current_logging_status == LOG_AND_CONTINUE) {
    strcpy(current_toplevel_function, name);
  } else if (current_logging_status == LOG_AND_STOP) {
    memset(current_toplevel_function, 0, MAX_FUNC_NAME_LEN);
  }
}

bool do_not_log() {
  return current_logging_status == DO_NOT_LOG;
}

void trace_logger_log_entry(char *func_name, int num_parameters) {
  if (!initp) {
    trace_logger_init();
  }

  // The opcode doesn't matter, as long as it's not RET_OP, and this
  // instrumentation function will only be inserted if we are in top-level mode
  // and the function is a tracked function.
  update_logging_status(func_name, 0, true, true);
  if (current_logging_status == DO_NOT_LOG)
    return;

  gzprintf(full_trace_file, "\nentry,%s,%d,\n", func_name, num_parameters);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid,
                       int opcode, bool is_tracked_function, bool is_toplevel_mode) {
  if (!initp) {
    trace_logger_init();
  }

  update_logging_status(name, opcode, is_tracked_function, is_toplevel_mode);
  if (current_logging_status == DO_NOT_LOG)
    return;

  gzprintf(full_trace_file, "\n0,%d,%s,%s,%s,%d,%d\n", line_number, name, bbid,
          instid, opcode, inst_count);
  inst_count++;
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg,
                          char *label, int is_phi, char *prev_bbid) {
  assert(initp == true);
  if (do_not_log())
    return;
  if (line == RESULT_LINE)
    gzprintf(full_trace_file, "r,%d,%ld,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(full_trace_file, "f,%d,%ld,%d", size, value, is_reg);
  else
    gzprintf(full_trace_file, "%d,%d,%ld,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(full_trace_file, ",%s", label);
  else
    gzprintf(full_trace_file, ", ");
  if (is_phi)
    gzprintf(full_trace_file, ",%s,\n", prev_bbid);
  else
    gzprintf(full_trace_file, ",\n");
}

void trace_logger_log_ptr(int line, int size, uint64_t value, int is_reg,
                          char *label, int is_phi, char *prev_bbid) {
  assert(initp == true);
  if (do_not_log())
    return;
  if (line == RESULT_LINE)
    gzprintf(full_trace_file, "r,%d,%#llx,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(full_trace_file, "f,%d,%#llx,%d", size, value, is_reg);
  else
    gzprintf(full_trace_file, "%d,%d,%#llx,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(full_trace_file, ",%s", label);
  else
    gzprintf(full_trace_file, ", ");
  if (is_phi)
    gzprintf(full_trace_file, ",%s,\n", prev_bbid);
  else
    gzprintf(full_trace_file, ",\n");
}

void trace_logger_log_double(int line, int size, double value, int is_reg,
                             char *label, int is_phi, char *prev_bbid) {
  assert(initp == true);
  if (do_not_log())
    return;
  if (line == RESULT_LINE)
    gzprintf(full_trace_file, "r,%d,%f,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(full_trace_file, "f,%d,%f,%d", size, value, is_reg);
  else
    gzprintf(full_trace_file, "%d,%d,%f,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(full_trace_file, ",%s", label);
  else
    gzprintf(full_trace_file, ", ");
  if (is_phi)
    gzprintf(full_trace_file, ",%s,\n", prev_bbid);
  else
    gzprintf(full_trace_file, ",\n");
}

void trace_logger_log_vector(int line, int size, uint8_t* value, int is_reg,
                             char *label, int is_phi, char *prev_bbid) {
  assert(initp == true);
  if (do_not_log())
    return;
  char value_str[size/4+3];  // +3 for "0x" and null termination.
  convert_bytes_to_hex(&value_str[0], value, size/8);
  if (line == RESULT_LINE)
    gzprintf(full_trace_file, "r,%d,%s,%d", size, value_str, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(full_trace_file, "f,%d,%s,%d", size, value_str, is_reg);
  else
    gzprintf(full_trace_file, "%d,%d,%s,%d", line, size, value_str, is_reg);
  if (is_reg)
    gzprintf(full_trace_file, ",%s", label);
  else
    gzprintf(full_trace_file, ", ");
  if (is_phi)
    gzprintf(full_trace_file, ",%s,\n", prev_bbid);
  else
    gzprintf(full_trace_file, ",\n");
}
