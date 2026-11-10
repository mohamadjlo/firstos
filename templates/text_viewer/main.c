#include "../../include/firstos/firstos_app.h"
#include "../../include/firstos/firstos_log.h"
#include "../../include/firstos/firstos_fs.h"

int main(int argc, char **argv) {
    firstos_log_info("TextViewer", "Starting Text Viewer application...");
    const char *target = (argc > 1) ? argv[1] : "/Desktop/notes.txt";
    firstos_app_print("[TextViewer] Opening file: ");
    firstos_app_println(target);

    int fd = firstos_fs_open(target, 0, 0);
    if (fd >= 0) {
        char buf[256];
        int n = firstos_fs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            firstos_app_println("----- File Content -----");
            firstos_app_println(buf);
            firstos_app_println("------------------------");
        } else {
            firstos_log_warn("TextViewer", "File was empty or read returned 0 bytes.");
        }
        firstos_fs_close(fd);
    } else {
        firstos_log_warn("TextViewer", "Could not open target file.");
    }
    firstos_log_info("TextViewer", "Text Viewer finished.");
    return 0;
}
