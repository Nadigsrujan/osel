#include <stdio.h>
#include <stdlib.h>
#include "../include/checkpoint.h"

int save_checkpoint(const char *filename, task_state_t *state) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open checkpoint file for writing");
        return -1;
    }
    
    size_t written = fwrite(state, sizeof(task_state_t), 1, fp);
    fclose(fp);
    
    if (written != 1) {
        fprintf(stderr, "Failed to write complete state\n");
        return -1;
    }
    
    printf("[Checkpoint] State saved to %s (Counter: %d)\n", filename, state->progress_counter);
    return 0;
}
