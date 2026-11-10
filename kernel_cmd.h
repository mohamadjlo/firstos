#ifndef KERNEL_CMD_H
#define KERNEL_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

void kernel_cmd_init(void);
void serial_poll_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CMD_H */
