#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#define RESULT_LINE 19134
#define FORWARD_LINE 24601

void trace_logger_init();
void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode);
void trace_logger_log_label();
void trace_logger_fin();

gzFile *full_trace_file;
int initp=0;

int inst_count = 0;
void trace_logger_init()
{
  full_trace_file=gzopen("dynamic_trace.gz","w");

  if (full_trace_file==NULL) {
    perror("Failed to open logfile \"dynamic_trace\"");
    exit(-1);
  }
  atexit(&trace_logger_fin);
}

void trace_logger_fin()
{
  gzclose(full_trace_file);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode)
{
  if (!initp) {
    trace_logger_init();
    initp=1;
  }
  gzprintf(full_trace_file, "\n0,%d,%s,%s,%s,%d,%d\n", line_number, name, bbid, instid, opcode,inst_count);
  inst_count++;
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg,
                          char *label, int is_phi, char *prev_bbid)
{
  assert(initp == 1);
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
void trace_logger_log_double(int line, int size, double value, int is_reg,
                             char *label, int is_phi, char *prev_bbid)
{
  assert(initp == 1);
  if (line == RESULT_LINE)
    gzprintf(full_trace_file, "r,%d,%f,%d", size, value, is_reg);
  else if (line == FORWARD_LINE)
    gzprintf(full_trace_file, "f,%d,%f,%d", size, value, is_reg);
  else
    gzprintf(full_trace_file, "%d,%d,%f,%d",line, size, value, is_reg);
  if (is_reg)
    gzprintf(full_trace_file, ",%s", label);
  else
    gzprintf(full_trace_file, ", ");
  if (is_phi)
    gzprintf(full_trace_file, ",%s,\n", prev_bbid);
  else
    gzprintf(full_trace_file, ",\n");
}
