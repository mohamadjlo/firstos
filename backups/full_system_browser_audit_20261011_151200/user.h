#ifndef USER_H
#define USER_H

#include "framebuffer.h"

#define MAX_USERS 8

typedef struct {
    char username[32];
    char display_name[32];
    char password[32];
    char hint[64];
    char avatar_path[128];
    u32 avatar_color;
    int is_root;       /* 1 = super user (administrator), 0 = standard user */
} user_profile_t;

void user_system_init(void);
int  user_get_count(void);
const user_profile_t* user_get_at(int index);
const user_profile_t* user_find(const char *username);
int  user_check_password(int user_index, const char *password);
int  user_get_active_index(void);
void user_set_active_index(int index);
const user_profile_t* user_get_active(void);
int  user_update_name(int user_index, const char *new_username, const char *new_display_name);
int  user_update_avatar(int user_index, const char *new_avatar_path);
int  user_update_password(int user_index, const char *new_password, const char *new_hint);
int  user_save_config(void);

#endif /* USER_H */
