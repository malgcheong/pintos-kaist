#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
struct container{
    struct file *file;
    off_t offset;
    size_t page_read_bytes;
};
/** #Project 3: Memory Mapped Files */
bool lazy_load_segment(struct page *page, void *aux);

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
