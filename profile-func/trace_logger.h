#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <pthread.h>
#include <map>

#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define RET_OP 1

enum logging_status {
  // Log the current instruction and continue logging.
  LOG_AND_CONTINUE,
  // Do not log the current instruction.
  DO_NOT_LOG,
};

struct trace_info {
  std::string trace_name;
  gzFile trace_file;
  int64_t inst_count;
  std::string current_toplevel_function;
  logging_status current_logging_status;

  trace_info(const char *_trace_name)
      : trace_name(_trace_name), inst_count(0),
        current_logging_status(DO_NOT_LOG) {}
};

void create_trace(const char *trace_name);
void write_labelmap();
void open_trace_file();
extern "C" {
  void trace_logger_init();
  void trace_logger_register_labelmap(const char *labelmap_buf,
                                      size_t labelmap_size);
  void trace_logger_log0(int line_number, char *name, char *bbid, char *instid,
                         int opcode, bool is_tracked_function,
                         bool is_toplevel_mode);
  void trace_logger_log_label();
  void trace_logger_log_entry(char *func_name, int num_parameters);
  void trace_logger_log_ptr(int line, int size, uint64_t value, int is_reg,
                            char *label, int is_phi, char *prev_bbid);
  void trace_logger_log_string(int line, int size, char *value, int is_reg,
                               char *label, int is_phi, char *prev_bbid);
  void trace_logger_log_int(int line, int size, int64_t value, int is_reg,
                            char *label, int is_phi, char *prev_bbid);
  void trace_logger_log_double(int line, int size, double value, int is_reg,
                               char *label, int is_phi, char *prev_bbid);
  void trace_logger_log_vector(int line, int size, uint8_t *value, int is_reg,
                               char *label, int is_phi, char *prev_bbid);
  void trace_logger_update_status(char *name, int opcode,
                                  bool is_tracked_function,
                                  bool is_toplevel_mode);
  void llvmtracer_set_trace_name(const char *trace_name);
}
void fin_main();
void fin_toplevel();
logging_status log_or_not(bool is_toplevel_mode, bool is_toplevel_function,
                          int opcode, char *current_function);
void convert_bytes_to_hex(char *buf, uint8_t *value, int size);
bool do_not_log();
