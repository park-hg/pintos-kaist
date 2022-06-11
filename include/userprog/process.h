#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

typedef int off_t;

struct file_info {
	struct file *file;
	off_t ofs;
	off_t file_size;
	size_t read_bytes;
	size_t zero_bytes;
};


tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

void argument_stack(char **arg, int count, struct intr_frame *if_);
struct thread *get_child_process(int pid);

#endif /* userprog/process.h */
