#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define RESULT_LINE 19134

void trace_logger_init();
void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode);
void trace_logger_log_label();
void trace_logger_fin();

//std::ofstream full_trace_file;
FILE *full_trace_file;
int initp=0;
void trace_logger_init()
{
//std::ofstream full_trace_file;
  full_trace_file=fopen("dynamic_trace","w");
  if (full_trace_file==NULL){ 
    perror("Failed to open logfile \"dynamic_trace\"");
    exit(-1);
  }
  atexit(&trace_logger_fin);
}

void trace_logger_fin()
{
  int i;

  fclose(full_trace_file);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode)
{
  if(!initp) {
    trace_logger_init();
    initp=1;
  }
  fprintf(stderr, "\n0,line_number=%d,name=%s,bbid=%s,instid=%s,opcode=%d\n", line_number, name, bbid, instid, opcode);
  fprintf(full_trace_file, "\n0,%d,%s,%s,%s,%d\n", line_number, name, bbid, instid, opcode);
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg, char *label)
{
  assert(initp == 1);
  fprintf(stderr, "%d,%d,%lld,%d,%s\n", line, size, value, is_reg, label);
	if(line==RESULT_LINE)	
    fprintf(full_trace_file, "r,%d,%lld,%d,%s\n", size, value, is_reg, label);
	else
  	fprintf(full_trace_file, "%d,%d,%lld,%d,%s\n", line, size, value, is_reg, label);
}
void trace_logger_log_double(int line, int size, double value, int is_reg, char *label)
{
  assert(initp == 1);
  fprintf(stderr, "r,%d,%f,%d,%s\n", size, value, is_reg, label);
	if(line==RESULT_LINE) 
    fprintf(full_trace_file, "r,%d,%f,%d,%s\n", size, value, is_reg, label);
  else
	  fprintf(full_trace_file, "%d,%d,%f,%d,%s\n",line, size, value, is_reg, label);
}

void trace_logger_log_int_noreg(int line, int size, int64_t value, int is_reg)
{
  assert(initp == 1);
  fprintf(stderr, "%d,%d,%lld,%d\n", line, size, value, is_reg);
	if(line==RESULT_LINE)	
    fprintf(full_trace_file, "r,%d,%lld,%d\n", size, value, is_reg);
	else
  	fprintf(full_trace_file, "%d,%d,%lld,%d\n", line, size, value, is_reg);
}
void trace_logger_log_double_noreg(int line, int size, double value, int is_reg)
{
  assert(initp == 1);
	fprintf(stderr, "%d,%d,%f,%d\n",line, size, value, is_reg);
	if(line==RESULT_LINE) 
    fprintf(full_trace_file, "r,%d,%f,%d\n", size, value, is_reg);
  else
	  fprintf(full_trace_file, "%d,%d,%f,%d\n",line, size, value, is_reg);
}

