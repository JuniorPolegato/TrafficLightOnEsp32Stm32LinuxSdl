#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* Memory */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (64 * 1024U)

/* OS */
#define LV_USE_OS               LV_OS_FREERTOS
#define LV_DEF_REFR_PERIOD      33
#define LV_DPI_DEF              130

/* Drawing */
#define LV_USE_DRAW_SW          1
#define LV_DRAW_BUF_ALIGN       32      /* matches the M7 D-cache line */
#define LV_USE_DRAW_DMA2D       0       /* set to 1 later for speed */

/* Logging / asserts */
#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1

/* Fonts used by the app */
#define LV_FONT_MONTSERRAT_10   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Widgets / layouts */
#define LV_USE_LABEL            1
#define LV_USE_FLEX             1
#define LV_USE_GRID             0

/* Off on MCU */
#define LV_USE_SDL              0
#define LV_BUILD_EXAMPLES       0
#define LV_USE_DEMO_WIDGETS     0

#endif /* LV_CONF_H */
