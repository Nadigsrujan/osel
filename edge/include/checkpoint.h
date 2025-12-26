#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <stdint.h>

// Structure to represent a process state (Simplified)
typedef struct {
    int pid;
    long task_id;
    int progress_counter;
    char state_label[64];
    uint64_t timestamp;
} task_state_t;

int save_checkpoint(const char *filename, task_state_t *state);
int restore_checkpoint(const char *filename, task_state_t *state);

#endif
