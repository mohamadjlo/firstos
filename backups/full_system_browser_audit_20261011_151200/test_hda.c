#include <stdio.h>

int main() {
    printf("Verb for Node 2 Format: %08x\n", (2 << 20) | (0x2 << 16) | 0x4011);
    return 0;
}
