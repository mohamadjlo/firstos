#include "user.h"
#include "fs.h"
#include "terminal.h"

static user_profile_t users[MAX_USERS];
static int user_count = 0;
static int active_user_index = 0;

static int u_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static void u_cpy(char *dst, const char *src, int max) {
    if (!dst || max <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

void user_system_init(void) {
    user_count = 0;

    /* Default User 1: Administrator (Super User) */
    u_cpy(users[user_count].username, "admin", sizeof(users[0].username));
    u_cpy(users[user_count].display_name, "Administrator", sizeof(users[0].display_name));
    u_cpy(users[user_count].password, "1234", sizeof(users[0].password));
    u_cpy(users[user_count].hint, "Default admin PIN is 1234", sizeof(users[0].hint));
    users[user_count].avatar_path[0] = 0;
    users[user_count].avatar_color = 0x2563EB; /* Royal Blue */
    users[user_count].is_root = 1;
    user_count++;

    /* Default User 2: Standard User */
    u_cpy(users[user_count].username, "user", sizeof(users[0].username));
    u_cpy(users[user_count].display_name, "Standard User", sizeof(users[0].display_name));
    u_cpy(users[user_count].password, "password", sizeof(users[0].password));
    u_cpy(users[user_count].hint, "Password is 'password'", sizeof(users[0].hint));
    users[user_count].avatar_path[0] = 0;
    users[user_count].avatar_color = 0x059669; /* Emerald Green */
    users[user_count].is_root = 0;
    user_count++;

    /* Default User 3: Guest Account */
    u_cpy(users[user_count].username, "guest", sizeof(users[0].username));
    u_cpy(users[user_count].display_name, "Guest Account", sizeof(users[0].display_name));
    u_cpy(users[user_count].password, "", sizeof(users[0].password));
    u_cpy(users[user_count].hint, "No password required (press Log In)", sizeof(users[0].hint));
    users[user_count].avatar_path[0] = 0;
    users[user_count].avatar_color = 0xD97706; /* Amber */
    users[user_count].is_root = 0;
    user_count++;

    /* If /System/users.cfg exists, we can optionally parse additional accounts */
    static char cfg_buf[2048];
    int bytes = fs_read_at("/System/users.cfg", cfg_buf, sizeof(cfg_buf) - 1);
    if (bytes > 0) {
        cfg_buf[bytes] = 0;
        int pos = 0;
        while (pos < bytes && user_count < MAX_USERS) {
            char line[256];
            int li = 0;
            while (pos < bytes && cfg_buf[pos] != '\n' && cfg_buf[pos] != '\r' && li < 255) {
                line[li++] = cfg_buf[pos++];
            }
            line[li] = 0;
            while (pos < bytes && (cfg_buf[pos] == '\n' || cfg_buf[pos] == '\r')) pos++;

            if (!line[0] || line[0] == '#') continue;

            /* format: username:password:display_name:hint:is_root:avatar_path */
            char u[32] = {0}, p[32] = {0}, d[32] = {0}, h[64] = {0}, av[128] = {0};
            int is_su = 0;
            int part = 0, idx = 0;

            for (int k = 0; line[k]; k++) {
                if (line[k] == ':') {
                    part++;
                    idx = 0;
                    continue;
                }
                if (part == 0 && idx < 31) u[idx++] = line[k];
                else if (part == 1 && idx < 31) p[idx++] = line[k];
                else if (part == 2 && idx < 31) d[idx++] = line[k];
                else if (part == 3 && idx < 63) h[idx++] = line[k];
                else if (part == 4) is_su = (line[k] == '1');
                else if (part == 5 && idx < 127) av[idx++] = line[k];
            }

            if (u[0]) {
                /* Check if updating existing user */
                int found = -1;
                for (int ex = 0; ex < user_count; ex++) {
                    if (u_eq(users[ex].username, u)) { found = ex; break; }
                }
                int target = (found >= 0) ? found : user_count;
                u_cpy(users[target].username, u, sizeof(users[0].username));
                if (p[0]) u_cpy(users[target].password, p, sizeof(users[0].password));
                if (d[0]) u_cpy(users[target].display_name, d, sizeof(users[0].display_name));
                if (h[0]) u_cpy(users[target].hint, h, sizeof(users[0].hint));
                users[target].is_root = is_su;
                if (av[0]) u_cpy(users[target].avatar_path, av, sizeof(users[0].avatar_path));
                if (target == user_count) user_count++;
            }
        }
    }

    active_user_index = 0;
}

int user_get_count(void) {
    if (user_count == 0) user_system_init();
    return user_count;
}

const user_profile_t* user_get_at(int index) {
    if (user_count == 0) user_system_init();
    if (index < 0 || index >= user_count) return 0;
    return &users[index];
}

const user_profile_t* user_find(const char *username) {
    if (user_count == 0) user_system_init();
    if (!username) return 0;
    for (int i = 0; i < user_count; i++) {
        if (u_eq(users[i].username, username)) return &users[i];
    }
    return 0;
}

int user_check_password(int user_index, const char *password) {
    if (user_count == 0) user_system_init();
    if (user_index < 0 || user_index >= user_count) return 0;
    const user_profile_t *u = &users[user_index];
    /* If password is empty in user profile, any password or empty is accepted */
    if (u->password[0] == 0) return 1;
    return u_eq(u->password, password);
}

int user_get_active_index(void) {
    return active_user_index;
}

void user_set_active_index(int index) {
    if (index >= 0 && index < user_count) {
        active_user_index = index;
    }
}

const user_profile_t* user_get_active(void) {
    if (user_count == 0) user_system_init();
    if (active_user_index < 0 || active_user_index >= user_count) active_user_index = 0;
    return &users[active_user_index];
}

static void u_cat(char *dst, const char *src, int max) {
    if (!dst || !src || max <= 0) return;
    int i = 0;
    while (dst[i] && i < max - 1) i++;
    int j = 0;
    while (src[j] && i < max - 1) {
        dst[i++] = src[j++];
    }
    dst[i] = 0;
}

int user_update_name(int user_index, const char *new_username, const char *new_display_name) {
    if (user_count == 0) user_system_init();
    if (user_index < 0 || user_index >= user_count) return 0;

    if (new_username && new_username[0]) {
        /* Verify no spaces or colons */
        for (int k = 0; new_username[k]; k++) {
            if (new_username[k] == ' ' || new_username[k] == ':') return 0;
        }
        /* Check if another user already has this username */
        for (int i = 0; i < user_count; i++) {
            if (i != user_index && u_eq(users[i].username, new_username)) return 0;
        }
        u_cpy(users[user_index].username, new_username, sizeof(users[0].username));
    }

    if (new_display_name && new_display_name[0]) {
        /* Verify no colons */
        for (int k = 0; new_display_name[k]; k++) {
            if (new_display_name[k] == ':') return 0;
        }
        u_cpy(users[user_index].display_name, new_display_name, sizeof(users[0].display_name));
    }

    if (user_index == active_user_index) {
        terminal_set_active_user(users[user_index].username, users[user_index].is_root);
    }

    user_save_config();
    return 1;
}

int user_update_avatar(int user_index, const char *new_avatar_path) {
    if (user_count == 0) user_system_init();
    if (user_index < 0 || user_index >= user_count) return 0;
    u_cpy(users[user_index].avatar_path, new_avatar_path ? new_avatar_path : "", sizeof(users[0].avatar_path));
    user_save_config();
    return 1;
}

int user_update_password(int user_index, const char *new_password, const char *new_hint) {
    if (user_count == 0) user_system_init();
    if (user_index < 0 || user_index >= user_count) return 0;
    if (!new_password) return 0;

    /* Verify no colons in password */
    for (int k = 0; new_password[k]; k++) {
        if (new_password[k] == ':') return 0;
    }

    u_cpy(users[user_index].password, new_password, sizeof(users[0].password));
    if (new_hint) {
        u_cpy(users[user_index].hint, new_hint, sizeof(users[0].hint));
    }
    user_save_config();
    return 1;
}

int user_save_config(void) {
    static char buf[2048];
    buf[0] = 0;
    u_cat(buf, "# FirstOS User Accounts Configuration\n", sizeof(buf));
    u_cat(buf, "# username:password:display_name:hint:is_root:avatar_path\n", sizeof(buf));
    for (int i = 0; i < user_count; i++) {
        char line[256]; line[0] = 0;
        u_cat(line, users[i].username, sizeof(line));
        u_cat(line, ":", sizeof(line));
        u_cat(line, users[i].password, sizeof(line));
        u_cat(line, ":", sizeof(line));
        u_cat(line, users[i].display_name, sizeof(line));
        u_cat(line, ":", sizeof(line));
        u_cat(line, users[i].hint, sizeof(line));
        u_cat(line, ":", sizeof(line));
        u_cat(line, users[i].is_root ? "1" : "0", sizeof(line));
        u_cat(line, ":", sizeof(line));
        u_cat(line, users[i].avatar_path, sizeof(line));
        u_cat(line, "\n", sizeof(line));
        u_cat(buf, line, sizeof(buf));
    }
    int len = 0; while (buf[len]) len++;
    return fs_write_at("/System/users.cfg", buf, (u32)len);
}
