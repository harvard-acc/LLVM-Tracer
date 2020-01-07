#include "trace_logger.h"

thread_local trace_info *trace = nullptr;
std::map<std::string, gzFile> gz_files;
pthread_mutex_t lock;
std::string labelmap_str;
const char* default_trace_name = "dynamic_trace.gz";

void create_trace(const char *trace_name) {
  assert(!trace && "Trace has already been created!");
  trace = new trace_info(trace_name);
}

void write_labelmap() {
  gzFile gz_file = trace->trace_file;
  const char *section_header = "%%%% LABEL MAP START %%%%\n";
  const char *section_footer = "%%%% LABEL MAP END %%%%\n\n";
  gzwrite(gz_file, section_header, strlen(section_header));
  gzwrite(gz_file, labelmap_str.c_str(), labelmap_str.length());
  gzwrite(gz_file, section_footer, strlen(section_footer));
}

void open_trace_file() {
  pthread_mutex_lock(&lock);
  if (gz_files.find(trace->trace_name) != gz_files.end()) {
    // If the trace file is already opened, obtain the file pointer.
    trace->trace_file = gz_files.at(trace->trace_name);
  } else {
    // Open a new trace file and write the label map to it.
    gzFile gz_file = gzopen(trace->trace_name.c_str(), "w");
    if (!gz_file) {
      perror("Failed to open logfile \"dynamic_trace\"");
      exit(-1);
    }
    gz_files[trace->trace_name] = gz_file;
    trace->trace_file = gz_file;
    write_labelmap();
  }
  pthread_mutex_unlock(&lock);
}

void trace_logger_register_labelmap(const char *labelmap_buf,
                                    size_t labelmap_size) {
  labelmap_str.assign(labelmap_buf, labelmap_size);
}

// Called from the main function.
void trace_logger_init() {
  if (pthread_mutex_init(&lock, NULL) != 0) {
    perror("Failed to initialize the mutex\n");
    exit(-1);
  }
  // Create a trace for the main thread.
  create_trace(default_trace_name);
  atexit(&fin_main);
}

// Called at the exit of the main function.
void fin_main() {
  if (trace)
    fin_toplevel();
  for (auto it = gz_files.begin(); it != gz_files.end(); ++it) {
    gzclose(it->second);
  }
}

// Called when a top-level function returns or at the main function exit.
void fin_toplevel() {
  delete trace;
  trace = nullptr;
}

// Called before calling a top-level function.
void llvmtracer_set_trace_name(const char *trace_name) {
  if (!trace)
    create_trace(trace_name);
  else
    trace->trace_name = trace_name;
}

// Determine whether to trace the current and next instructions.
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
//    1     1     1         DO_NOT_LOG
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
    return trace->current_logging_status;

  if (opcode != RET_OP)
    return LOG_AND_CONTINUE;

  if (trace->current_toplevel_function.length() == 0)
    assert(false &&
           "Returning from within a toplevel function before it was called!");

  if (strcmp(current_function, trace->current_toplevel_function.c_str()) == 0)
    return DO_NOT_LOG;

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

// This function is called after every return instruction to update the
// tracer's status - that is, whether or not it should keep tracing.
void trace_logger_update_status(char *name, int opcode,
                                bool is_tracked_function,
                                bool is_toplevel_mode) {
  if (!trace) {
    if (is_tracked_function)
      create_trace(default_trace_name);
    else
      return;
  }
  logging_status temp = trace->current_logging_status;
  trace->current_logging_status =
      log_or_not(is_toplevel_mode, is_tracked_function, opcode, name);

  if (temp == LOG_AND_CONTINUE && trace->current_logging_status == DO_NOT_LOG) {
    printf("%s: Stopping logging at inst %ld.\n", trace->trace_name.c_str(),
           trace->inst_count);
    fflush(stdout);
  }

  if (temp == DO_NOT_LOG && trace->current_logging_status != temp) {
    printf("%s: Starting to log at inst = %ld.\n", trace->trace_name.c_str(),
           trace->inst_count);
    fflush(stdout);
  }

  if (trace->current_toplevel_function.length() == 0 &&
      trace->current_logging_status == LOG_AND_CONTINUE) {
    trace->current_toplevel_function = name;
  } else if (trace->current_logging_status == DO_NOT_LOG) {
    trace->current_toplevel_function = "";
    fin_toplevel();
  }
}

bool do_not_log() {
  if (!trace)
    return true;
  return trace->current_logging_status == DO_NOT_LOG;
}

// Prints an entry block upon calling a top level function. This also needs to
// reinitialize the trace state, since the last top level function exit would
// have deleted it.
void trace_logger_log_entry(char *func_name, int num_parameters) {
  if (!trace) {
    create_trace(default_trace_name);
  }

  if (do_not_log())
    return;

  open_trace_file();
  gzprintf(trace->trace_file, "\nentry,%s,%d,\n", func_name, num_parameters);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid,
                       int opcode, bool is_tracked_function, bool is_toplevel_mode) {
  if (!trace)
    return;

  if (do_not_log())
    return;

  gzprintf(trace->trace_file, "\n0,%d,%s,%s,%s,%d,%ld\n", line_number, name,
           bbid, instid, opcode, trace->inst_count);
  trace->inst_count++;
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg,
                          char *label, int is_phi, char *prev_bbid) {
  if (!trace || do_not_log())
    return;

  gzFile gz_file = trace->trace_file;

  if (line == RESULT_LINE)
    gzprintf(gz_file, "r,%d,%ld,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(gz_file, "f,%d,%ld,%d", size, value, is_reg);
  else
    gzprintf(gz_file, "%d,%d,%ld,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(gz_file, ",%s", label);
  else
    gzprintf(gz_file, ", ");
  if (is_phi)
    gzprintf(gz_file, ",%s,\n", prev_bbid);
  else
    gzprintf(gz_file, ",\n");
}

void trace_logger_log_ptr(int line, int size, uint64_t value, int is_reg,
                          char *label, int is_phi, char *prev_bbid) {
  if (!trace || do_not_log())
    return;

  gzFile gz_file = trace->trace_file;

  if (line == RESULT_LINE)
    gzprintf(gz_file, "r,%d,%#llx,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(gz_file, "f,%d,%#llx,%d", size, value, is_reg);
  else
    gzprintf(gz_file, "%d,%d,%#llx,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(gz_file, ",%s", label);
  else
    gzprintf(gz_file, ", ");
  if (is_phi)
    gzprintf(gz_file, ",%s,\n", prev_bbid);
  else
    gzprintf(gz_file, ",\n");
}

void trace_logger_log_string(int line,
                             int size,
                             char *value,
                             int is_reg,
                             char *label,
                             int is_phi,
                             char *prev_bbid) {
  if (!trace || do_not_log())
    return;

  gzFile gz_file = trace->trace_file;

  if (line == RESULT_LINE)
    gzprintf(gz_file, "r,%d,%s,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(gz_file, "f,%d,%s,%d", size, value, is_reg);
  else
    gzprintf(gz_file, "%d,%d,%s,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(gz_file, ",%s", label);
  else
    gzprintf(gz_file, ", ");
  if (is_phi)
    gzprintf(gz_file, ",%s,\n", prev_bbid);
  else
    gzprintf(gz_file, ",\n");
}

void trace_logger_log_double(int line, int size, double value, int is_reg,
                             char *label, int is_phi, char *prev_bbid) {
  if (!trace || do_not_log())
    return;

  gzFile gz_file = trace->trace_file;

  if (line == RESULT_LINE)
    gzprintf(gz_file, "r,%d,%f,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(gz_file, "f,%d,%f,%d", size, value, is_reg);
  else
    gzprintf(gz_file, "%d,%d,%f,%d", line, size, value, is_reg);
  if (is_reg)
    gzprintf(gz_file, ",%s", label);
  else
    gzprintf(gz_file, ", ");
  if (is_phi)
    gzprintf(gz_file, ",%s,\n", prev_bbid);
  else
    gzprintf(gz_file, ",\n");
}

void trace_logger_log_vector(int line, int size, uint8_t* value, int is_reg,
                             char *label, int is_phi, char *prev_bbid) {
  if (!trace || do_not_log())
    return;

  char value_str[size/4+3];  // +3 for "0x" and null termination.
  convert_bytes_to_hex(&value_str[0], value, size/8);

  gzFile gz_file = trace->trace_file;

  if (line == RESULT_LINE)
    gzprintf(gz_file, "r,%d,%s,%d", size, value_str, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(gz_file, "f,%d,%s,%d", size, value_str, is_reg);
  else
    gzprintf(gz_file, "%d,%d,%s,%d", line, size, value_str, is_reg);
  if (is_reg)
    gzprintf(gz_file, ",%s", label);
  else
    gzprintf(gz_file, ", ");
  if (is_phi)
    gzprintf(gz_file, ",%s,\n", prev_bbid);
  else
    gzprintf(gz_file, ",\n");
}
