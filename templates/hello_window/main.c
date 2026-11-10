#include "../../include/firstos/firstos_app.h"
#include "../../include/firstos/firstos_log.h"
#include "../../include/firstos/firstos_ui.h"

int main(int argc, char **argv) {
    firstos_log_info("HelloApp", "Hello Window App started successfully from FirstOS userspace!");
    firstos_app_print("[HelloApp] Arguments received: argc = ");
    char c = '0' + (char)argc;
    firstos_write(1, &c, 1);
    firstos_app_print("\n");
    for (int i = 0; i < argc; i++) {
        firstos_app_print("  argv: ");
        firstos_app_println(argv[i]);
    }

    firstos_ui_window_config_t win;
    firstos_ui_init_window(&win, "Hello Window App", 480, 320);
    firstos_log_info("HelloApp", "Configured native UI window: 480x320");

    firstos_app_println("[HelloApp] Entering application event loop...");
    while (1) {
        firstos_app_yield();
    }
    return 0;
}
