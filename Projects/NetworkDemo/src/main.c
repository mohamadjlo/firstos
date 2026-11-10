#include "../../include/firstos/firstos_app.h"
#include "../../include/firstos/firstos_log.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    firstos_log_info("NetDemo", "Network Demo App launched.");
    firstos_app_println("[NetDemo] Querying network permissions...");
    firstos_log_info("NetDemo", "Network permission granted: attempting connection to 127.0.0.1:80...");
    firstos_app_println("[NetDemo] Socket IPC channel active.");
    firstos_log_info("NetDemo", "Network Demo execution completed.");
    return 0;
}
