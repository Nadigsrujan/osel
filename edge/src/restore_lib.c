#include <stdio.h>
#include <stdlib.h>
#include "../include/checkpoint.h"

int restore_checkpoint(const char *filename, task_state_t *state) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open checkpoint file for reading");
        return -1;
    }
    
    size_t read = fread(state, sizeof(task_state_t), 1, fp);
    fclose(fp);
    
    if (read != 1) {
        fprintf(stderr, "Failed to read complete state\n");
        return -1;
    }
    
    printf("[Restore] State restored from %s (Counter: %d)\n", filename, state->progress_counter);
    return 0;
}
