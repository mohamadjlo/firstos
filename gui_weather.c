#include "gui_weather.h"
#include "framebuffer.h"
#include "fs.h"
#include "rtc.h"
#include "net.h"
#include "http_client.h"

weather_info_t current_weather = {
    "New York",
    WEATHER_SUNNY,
    22,
    48,
    14,
    "Sunny"
};

static int weather_match_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    int hl = 0, nl = 0;
    while (haystack[hl]) hl++;
    while (needle[nl]) nl++;
    if (hl < nl) return 0;
    for (int i = 0; i <= hl - nl; i++) {
        int match = 1;
        for (int j = 0; j < nl; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static int s_weather_is_live = 0;
int weather_is_live(void) {
    return s_weather_is_live;
}

static char *weather_find_char(char *str, char c) {
    while (*str) {
        if (*str == c) return str;
        str++;
    }
    return (char*)0;
}

static void weather_copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int weather_fetch_realtime(const char *city_override) {
    if (!net_is_connected()) return 0;

    char url[128];
    const char *target_city = city_override ? city_override : current_weather.city;
    int ui = 0;
    const char *pfx = "http://wttr.in/";
    while (*pfx && ui < 120) url[ui++] = *pfx++;
    if (target_city && target_city[0]) {
        for (int i = 0; target_city[i] && ui < 100; i++) {
            if (target_city[i] == ' ') url[ui++] = '+';
            else url[ui++] = target_city[i];
        }
    }
    const char *sfx = "?format=%l|%C|%t|%h|%w";
    while (*sfx && ui < 127) url[ui++] = *sfx++;
    url[ui] = 0;

    static char resp[512];
    int n = http_get(url, resp, sizeof(resp) - 1);
    if (n <= 0) return 0;
    resp[n] = 0;

    char *p = resp;
    char *pipe1 = weather_find_char(p, '|');
    if (!pipe1) return 0;
    *pipe1 = 0;

    char *city_str = p;
    while (*city_str == ' ') city_str++;
    char *end_city = pipe1 - 1;
    while (end_city > city_str && (*end_city == ' ' || *end_city == '\r' || *end_city == '\n')) *end_city-- = 0;
    if (*city_str) {
        int ci = 0;
        while (city_str[ci] && city_str[ci] != ',' && ci < 31) {
            current_weather.city[ci] = city_str[ci];
            ci++;
        }
        current_weather.city[ci] = 0;
    }

    p = pipe1 + 1;
    char *pipe2 = weather_find_char(p, '|');
    if (!pipe2) return 0;
    *pipe2 = 0;
    char *cond_str = p;
    while (*cond_str == ' ') cond_str++;
    char *end_cond = pipe2 - 1;
    while (end_cond > cond_str && (*end_cond == ' ' || *end_cond == '\r' || *end_cond == '\n')) *end_cond-- = 0;
    static char s_desc[32];
    weather_copy_str(s_desc, cond_str, sizeof(s_desc));
    current_weather.desc = s_desc;

    if (weather_match_ci(cond_str, "rain") || weather_match_ci(cond_str, "shower") || 
        weather_match_ci(cond_str, "drizzle") || weather_match_ci(cond_str, "storm") ||
        weather_match_ci(cond_str, "thunder") || weather_match_ci(cond_str, "snow")) {
        current_weather.condition = WEATHER_RAINY;
    } else if (weather_match_ci(cond_str, "cloud") || weather_match_ci(cond_str, "overcast") || 
               weather_match_ci(cond_str, "fog") || weather_match_ci(cond_str, "mist") ||
               weather_match_ci(cond_str, "haze")) {
        current_weather.condition = WEATHER_CLOUDY;
    } else {
        current_weather.condition = WEATHER_SUNNY;
    }

    p = pipe2 + 1;
    char *pipe3 = weather_find_char(p, '|');
    if (!pipe3) return 0;
    *pipe3 = 0;
    char *temp_str = p;
    while (*temp_str && *temp_str != '-' && *temp_str != '+' && (*temp_str < '0' || *temp_str > '9')) temp_str++;
    int sign = 1;
    if (*temp_str == '-') { sign = -1; temp_str++; }
    else if (*temp_str == '+') { temp_str++; }
    int tval = 0;
    while (*temp_str >= '0' && *temp_str <= '9') {
        tval = tval * 10 + (*temp_str - '0');
        temp_str++;
    }
    current_weather.temp_c = sign * tval;

    p = pipe3 + 1;
    char *pipe4 = weather_find_char(p, '|');
    if (!pipe4) return 0;
    *pipe4 = 0;
    char *hum_str = p;
    while (*hum_str && (*hum_str < '0' || *hum_str > '9')) hum_str++;
    int hval = 0;
    while (*hum_str >= '0' && *hum_str <= '9') {
        hval = hval * 10 + (*hum_str - '0');
        hum_str++;
    }
    if (hval >= 0 && hval <= 100) current_weather.humidity = hval;

    p = pipe4 + 1;
    char *wind_str = p;
    while (*wind_str && (*wind_str < '0' || *wind_str > '9')) wind_str++;
    int wval = 0;
    while (*wind_str >= '0' && *wind_str <= '9') {
        wval = wval * 10 + (*wind_str - '0');
        wind_str++;
    }
    if (wval >= 0) current_weather.wind_kph = wval;

    s_weather_is_live = 1;
    return 1;
}

void weather_set_city(const char *city_or_zip) {
    if (!city_or_zip || !city_or_zip[0]) return;
    int i = 0;
    while (city_or_zip[i] && i < 31) {
        current_weather.city[i] = city_or_zip[i];
        i++;
    }
    current_weather.city[i] = 0;

    if (net_is_connected()) {
        if (weather_fetch_realtime(city_or_zip)) return;
    }

    if (weather_match_ci(city_or_zip, "york") || weather_match_ci(city_or_zip, "nyc") || weather_match_ci(city_or_zip, "10001")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 22;
        current_weather.humidity = 48;
        current_weather.wind_kph = 14;
        current_weather.desc = "Sunny";
    } else if (weather_match_ci(city_or_zip, "london") || weather_match_ci(city_or_zip, "lon") || weather_match_ci(city_or_zip, "sw1")) {
        current_weather.condition = WEATHER_RAINY;
        current_weather.temp_c = 15;
        current_weather.humidity = 84;
        current_weather.wind_kph = 22;
        current_weather.desc = "Rainy";
    } else if (weather_match_ci(city_or_zip, "tokyo") || weather_match_ci(city_or_zip, "tyo")) {
        current_weather.condition = WEATHER_CLOUDY;
        current_weather.temp_c = 19;
        current_weather.humidity = 62;
        current_weather.wind_kph = 11;
        current_weather.desc = "Cloudy";
    } else if (weather_match_ci(city_or_zip, "paris") || weather_match_ci(city_or_zip, "par") || weather_match_ci(city_or_zip, "75001")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 23;
        current_weather.humidity = 50;
        current_weather.wind_kph = 13;
        current_weather.desc = "Clear";
    } else if (weather_match_ci(city_or_zip, "berlin") || weather_match_ci(city_or_zip, "ber") || weather_match_ci(city_or_zip, "10115")) {
        current_weather.condition = WEATHER_CLOUDY;
        current_weather.temp_c = 17;
        current_weather.humidity = 68;
        current_weather.wind_kph = 18;
        current_weather.desc = "Overcast";
    } else if (weather_match_ci(city_or_zip, "francisco") || weather_match_ci(city_or_zip, "sf") || weather_match_ci(city_or_zip, "94102")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 18;
        current_weather.humidity = 55;
        current_weather.wind_kph = 19;
        current_weather.desc = "Sunny & Breezy";
    } else {
        u32 h = 5381;
        for (int k = 0; city_or_zip[k]; k++) {
            h = ((h << 5) + h) + (u8)city_or_zip[k];
        }
        current_weather.condition = (weather_cond_t)(h % 3);
        current_weather.temp_c = 14 + (int)(h % 16);
        current_weather.humidity = 40 + (int)((h >> 4) % 50);
        current_weather.wind_kph = 8 + (int)((h >> 8) % 20);
        if (current_weather.condition == WEATHER_SUNNY) current_weather.desc = "Sunny";
        else if (current_weather.condition == WEATHER_CLOUDY) current_weather.desc = "Cloudy";
        else current_weather.desc = "Light Rain";
    }
}

void weather_tick(void) {
    static int check_counter = 0;
    static int net_poll_counter = 0;
    check_counter++;

    if (check_counter >= 2) {
        check_counter = 0;
        char wbuf[256];
        int n = fs_read_at("/weather.txt", wbuf, sizeof(wbuf) - 1);
        if (n <= 0) {
            n = fs_read_at("/Desktop/weather.txt", wbuf, sizeof(wbuf) - 1);
        }
        if (n > 0) {
            wbuf[n] = 0;
            char *line = wbuf;
            while (*line) {
                char *eol = line;
                while (*eol && *eol != '\n' && *eol != '\r') eol++;
                char orig_end = *eol;
                *eol = 0;

                while (*line == ' ') line++;
                if (weather_match_ci(line, "CITY=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int vi = 0;
                    while (val[vi] && vi < 31) { current_weather.city[vi] = val[vi]; vi++; }
                    current_weather.city[vi] = 0;
                } else if (weather_match_ci(line, "TEMP=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int sign = 1, tval = 0;
                    if (*val == '-') { sign = -1; val++; }
                    while (*val >= '0' && *val <= '9') { tval = tval * 10 + (*val - '0'); val++; }
                    current_weather.temp_c = sign * tval;
                } else if (weather_match_ci(line, "COND=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    if (weather_match_ci(val, "rain") || weather_match_ci(val, "storm")) {
                        current_weather.condition = WEATHER_RAINY;
                        current_weather.desc = "Rainy";
                    } else if (weather_match_ci(val, "cloud") || weather_match_ci(val, "fog") || weather_match_ci(val, "overcast")) {
                        current_weather.condition = WEATHER_CLOUDY;
                        current_weather.desc = "Cloudy";
                    } else {
                        current_weather.condition = WEATHER_SUNNY;
                        current_weather.desc = "Sunny";
                    }
                } else if (weather_match_ci(line, "HUMIDITY=") || weather_match_ci(line, "HUM=")) {
                    char *val = line + (weather_match_ci(line, "HUM=") ? 4 : 9);
                    while (*val == ' ') val++;
                    int hval = 0;
                    while (*val >= '0' && *val <= '9') { hval = hval * 10 + (*val - '0'); val++; }
                    if (hval >= 0 && hval <= 100) current_weather.humidity = hval;
                } else if (weather_match_ci(line, "WIND=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int wval = 0;
                    while (*val >= '0' && *val <= '9') { wval = wval * 10 + (*val - '0'); val++; }
                    if (wval >= 0) current_weather.wind_kph = wval;
                } else if (weather_match_ci(line, "DESC=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    static char desc_buf[32];
                    int di = 0;
                    while (val[di] && di < 31) { desc_buf[di] = val[di]; di++; }
                    desc_buf[di] = 0;
                    current_weather.desc = desc_buf;
                }

                if (!orig_end) break;
                line = eol + 1;
                while (*line == '\r' || *line == '\n') line++;
            }
            s_weather_is_live = 1;
            return;
        }
    }

    net_poll_counter++;
    if (net_is_connected()) {
        if (!s_weather_is_live || net_poll_counter >= 300) {
            net_poll_counter = 0;
            weather_fetch_realtime((const char*)0);
        }
    }

    if (s_weather_is_live) return;

    rtc_time_t rtc;
    rtc_read(&rtc);
    int base_temp = 18;
    if (weather_match_ci(current_weather.city, "london")) base_temp = 14;
    else if (weather_match_ci(current_weather.city, "tokyo")) base_temp = 17;
    else if (weather_match_ci(current_weather.city, "paris")) base_temp = 19;
    else if (weather_match_ci(current_weather.city, "francisco")) base_temp = 16;
    else if (weather_match_ci(current_weather.city, "berlin")) base_temp = 15;

    int hour = rtc.hour;
    int delta = 0;
    if (hour >= 6 && hour < 14) {
        delta = ((hour - 6) * 6) / 8;
    } else if (hour >= 14 && hour < 21) {
        delta = 6 - ((hour - 14) * 5) / 7;
    } else {
        delta = -2;
    }
    current_weather.temp_c = base_temp + delta;
}

void draw_weather_icon(int cx, int cy, weather_cond_t cond, int size, u32 def_color) {
    (void)def_color;
    if (cond == WEATHER_SUNNY) {
        u32 sun_gold = 0xF59E0B;
        int r = size >= 24 ? 6 : 4;
        fb_round_rect(cx - r, cy - r, r * 2, r * 2, r, sun_gold);
        int ray_len = size >= 24 ? 3 : 2;
        int dist = r + 2;
        fb_rect(cx - 1, cy - dist - ray_len, 2, ray_len, sun_gold);
        fb_rect(cx - 1, cy + dist, 2, ray_len, sun_gold);
        fb_rect(cx - dist - ray_len, cy - 1, ray_len, 2, sun_gold);
        fb_rect(cx + dist, cy - 1, ray_len, 2, sun_gold);
        if (size >= 20) {
            int d = r + 1;
            fb_rect(cx - d - 1, cy - d - 1, 2, 2, sun_gold);
            fb_rect(cx + d, cy - d - 1, 2, 2, sun_gold);
            fb_rect(cx - d - 1, cy + d, 2, 2, sun_gold);
            fb_rect(cx + d, cy + d, 2, 2, sun_gold);
        }
    } else if (cond == WEATHER_CLOUDY) {
        u32 cloud_color = 0xCBD5E1;
        u32 cloud_shade = 0x94A3B8;
        if (size >= 24) {
            fb_round_rect(cx - 12, cy - 2, 24, 12, 6, cloud_color);
            fb_round_rect(cx - 8, cy - 8, 12, 12, 6, cloud_color);
            fb_round_rect(cx + 1, cy - 6, 10, 10, 5, cloud_color);
            fb_round_rect_border(cx - 12, cy - 2, 24, 12, 6, cloud_shade);
        } else {
            fb_round_rect(cx - 7, cy - 1, 14, 7, 3, cloud_color);
            fb_round_rect(cx - 4, cy - 5, 8, 8, 4, cloud_color);
            fb_round_rect(cx + 1, cy - 3, 6, 6, 3, cloud_color);
        }
    } else {
        u32 cloud_color = 0x94A3B8;
        u32 rain_color = 0x38BDF8;
        if (size >= 24) {
            fb_round_rect(cx - 12, cy - 8, 24, 10, 5, cloud_color);
            fb_round_rect(cx - 7, cy - 12, 11, 10, 5, cloud_color);
            fb_rect(cx - 8, cy + 4, 2, 5, rain_color);
            fb_rect(cx - 3, cy + 6, 2, 5, rain_color);
            fb_rect(cx + 2, cy + 4, 2, 5, rain_color);
            fb_rect(cx + 7, cy + 6, 2, 5, rain_color);
        } else {
            fb_round_rect(cx - 6, cy - 5, 13, 6, 3, cloud_color);
            fb_round_rect(cx - 3, cy - 8, 7, 7, 3, cloud_color);
            fb_rect(cx - 4, cy + 2, 1, 3, rain_color);
            fb_rect(cx, cy + 3, 1, 3, rain_color);
            fb_rect(cx + 4, cy + 2, 1, 3, rain_color);
        }
    }
}
