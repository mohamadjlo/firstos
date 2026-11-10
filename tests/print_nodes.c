#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE *f = popen("./tests/test_google_layout", "r");
    return 0;
}
