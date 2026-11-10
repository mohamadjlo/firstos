#ifndef GUI_WEATHER_H
#define GUI_WEATHER_H

#include "gui_internal.h"

extern weather_info_t current_weather;

int  weather_is_live(void);
int  weather_fetch_realtime(const char *city_override);
void weather_set_city(const char *city_or_zip);
void weather_tick(void);
void draw_weather_icon(int cx, int cy, weather_cond_t cond, int size, u32 def_color);

#endif
