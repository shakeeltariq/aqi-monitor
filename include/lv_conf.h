/* Minimal LVGL 9 configuration. Anything not set here takes LVGL's default
 * from lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* LVGL's assembly files (.S) also include this header, so C-only content
 * must stay out of the assembler's view. */
#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

#define LV_COLOR_DEPTH 16

/* Use the C library allocator. On this board, allocations above ~4 KB are
 * placed in PSRAM automatically; small LVGL objects stay in internal RAM. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16   /* ~60 fps cap */
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE    /* all LVGL calls happen on the Arduino loop task */

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

/* Fonts (built-in Montserrat: ASCII + LV_SYMBOL_* icons only) */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_USE_PERF_MONITOR 0

#endif /* LV_CONF_H */
