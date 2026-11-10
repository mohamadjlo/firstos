#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void spa_unescape_json(const char *in, int in_len, char *out, int max_out) {
    int o = 0;
    for (int i = 0; i < in_len && o < max_out - 1; i++) {
        if (in[i] == '\\' && i + 1 < in_len) {
            char next = in[i + 1];
            if (next == '"') { out[o++] = '"'; i++; }
            else if (next == '\\') { out[o++] = '\\'; i++; }
            else if (next == '/') { out[o++] = '/'; i++; }
            else if (next == 'n' || next == 'r' || next == 't') { out[o++] = ' '; i++; }
            else if (next == 'u' && i + 5 < in_len) {
                if (strncmp(in + i + 2, "0026", 4) == 0) { out[o++] = '&'; i += 5; }
                else if (strncmp(in + i + 2, "003c", 4) == 0) { out[o++] = '<'; i += 5; }
                else if (strncmp(in + i + 2, "003e", 4) == 0) { out[o++] = '>'; i += 5; }
                else if (strncmp(in + i + 2, "0027", 4) == 0) { out[o++] = '\''; i += 5; }
                else if (strncmp(in + i + 2, "0022", 4) == 0) { out[o++] = '"'; i += 5; }
                else { i += 5; }
            } else {
                i++;
            }
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

static int spa_extract_json_val(const char *chunk, int chunk_len, const char *key, char *out, int max_out) {
    if (!chunk || chunk_len <= 0 || !key || !out || max_out <= 1) return 0;
    out[0] = '\0';
    const char *pos = strstr(chunk, key);
    if (!pos || (pos - chunk) >= chunk_len) return 0;

    pos += strlen(key);
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    if (*pos != '"') return 0;
    pos++;

    char raw[256];
    int rlen = 0;
    while (*pos && *pos != '"' && (pos - chunk) < chunk_len && rlen < (int)sizeof(raw) - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            raw[rlen++] = *pos++;
        }
        raw[rlen++] = *pos++;
    }
    raw[rlen] = '\0';
    spa_unescape_json(raw, rlen, out, max_out);
    return (rlen > 0);
}

static int spa_find_json_field(const char *chunk, int chunk_len, const char *obj_key, const char *field_key, char *out, int max_out) {
    if (!chunk || chunk_len <= 0 || !obj_key || !field_key || !out || max_out <= 1) return 0;
    out[0] = '\0';
    const char *pos = strstr(chunk, obj_key);
    if (!pos || (pos - chunk) >= chunk_len) return 0;

    int rem = chunk_len - (int)(pos - chunk);
    if (rem > 2500) rem = 2500;

    const char *fpos = strstr(pos, field_key);
    if (!fpos || (fpos - pos) >= rem) return 0;

    fpos += strlen(field_key);
    while (*fpos == ' ' || *fpos == '\t' || *fpos == '\r' || *fpos == '\n') fpos++;
    if (*fpos != '"') return 0;
    fpos++;

    char raw[256];
    int rlen = 0;
    while (*fpos && *fpos != '"' && (fpos - chunk) < chunk_len && rlen < (int)sizeof(raw) - 1) {
        if (*fpos == '\\' && *(fpos + 1)) {
            raw[rlen++] = *fpos++;
        }
        raw[rlen++] = *fpos++;
    }
    raw[rlen] = '\0';
    spa_unescape_json(raw, rlen, out, max_out);
    return (rlen > 0);
}

int main() {
    FILE *f = fopen("/tmp/yt_watch.html", "rb");
    if (!f) { printf("Failed to open file\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *html = malloc(len + 1);
    fread(html, 1, len, f);
    html[len] = '\0';
    fclose(f);

    const char *base_url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
    int is_watch = 1;
    char main_vid[16] = "dQw4w9WgXcQ";

    char seen_ids[64][16];
    int card_count = 0;
    const char *pos = html;
    const char *html_end = html + len;

    while (pos < html_end - 15 && card_count < 24) {
        const char *vpos = strstr(pos, "\"videoId\":\"");
        if (!vpos || vpos >= html_end - 15) break;

        vpos += 11;
        char vid[16];
        int vi = 0;
        while (vpos < html_end && *vpos != '"' && vi < 11) {
            vid[vi++] = *vpos++;
        }
        vid[vi] = '\0';
        pos = vpos;

        if (vi != 11) continue;
        if (is_watch && main_vid[0] && strcmp(vid, main_vid) == 0) continue;

        int already = 0;
        for (int k = 0; k < card_count; k++) {
            if (strcmp(seen_ids[k], vid) == 0) {
                already = 1;
                break;
            }
        }
        if (already) continue;

        /* Window around vid: 800 bytes before, 2500 bytes after */
        const char *w_start = (vpos - 11 - 800 >= html) ? (vpos - 11 - 800) : html;
        const char *w_end = (vpos + 2500 <= html_end) ? (vpos + 2500) : html_end;
        int w_len = (int)(w_end - w_start);

        char title[192] = "";
        char owner[96] = "";
        char views[64] = "";
        char duration[32] = "";

        if (!spa_find_json_field(w_start, w_len, "\"title\":", "\"simpleText\":\"", title, sizeof(title))) {
            spa_find_json_field(w_start, w_len, "\"title\":", "\"text\":\"", title, sizeof(title));
        }
        if (!title[0]) {
            spa_find_json_field(w_start, w_len, "\"label\":\"", "", title, sizeof(title));
        }
        if (!title[0]) continue;

        if (!spa_find_json_field(w_start, w_len, "\"ownerText\":", "\"text\":\"", owner, sizeof(owner))) {
            spa_find_json_field(w_start, w_len, "\"longBylineText\":", "\"text\":\"", owner, sizeof(owner));
        }
        if (!owner[0]) strcpy(owner, "YouTube Creator");

        if (!spa_find_json_field(w_start, w_len, "\"metadata\":", "\"simpleText\":\"", views, sizeof(views))) {
            if (!spa_find_json_field(w_start, w_len, "\"shortViewCountText\":", "\"simpleText\":\"", views, sizeof(views))) {
                spa_find_json_field(w_start, w_len, "\"viewCountText\":", "\"simpleText\":\"", views, sizeof(views));
            }
        }

        spa_find_json_field(w_start, w_len, "\"lengthText\":", "\"simpleText\":\"", duration, sizeof(duration));

        strncpy(seen_ids[card_count], vid, sizeof(seen_ids[card_count]) - 1);
        printf("[%d] Found card: %s | %s | %s | %s\n", card_count, vid, title, owner, views);
        card_count++;
    }

    printf("Total cards extracted: %d\n", card_count);
    free(html);
    return 0;
}
