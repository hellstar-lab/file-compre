#include <stdio.h>

// CLI-only build stub for GUI entrypoint to satisfy linker
int StartGUI() {
    fprintf(stderr, "GUI mode is not available in this build.\n");
    return -1;
}