#ifndef LINUX_ELF_H
#define LINUX_ELF_H

int linux_elf_load_and_run(const char *path, const char *arg);
int linux_spawn(const char *binary_path, const char *arg);

#endif
