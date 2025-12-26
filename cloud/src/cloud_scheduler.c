#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "../../edge/include/checkpoint.h"

int main() {
    printf("Cloud Linux Runtime: Waiting for task migration...\n");
    
    task_state_t migrated_state;
    const char *state_file = "/tmp/task_state.bin";

    // Loop until state file appears (simulating socket reception)
    while (access(state_file, F_OK) != 0) {
        printf("Waiting for incoming state...\n");
        sleep(2);
    }

    printf("[RECEIVE] New task state received!\n");

    if (restore_checkpoint(state_file, &migrated_state) == 0) {
        printf("[RESUME] Task %s (PID: %d) resumed on Cloud at progress %d\n", 
               migrated_state.state_label, migrated_state.pid, migrated_state.progress_counter);
        
        // Continue task execution
        for (int i = migrated_state.progress_counter; i < 100; i++) {
            printf("[CLOUD] Task running... Progress: %d%%\n", i);
            sleep(1);
        }
        
        printf("[FINISH] Migrated task completed successfully on Cloud.\n");
    }

    // Cleanup
    remove(state_file);
    return 0;
}
