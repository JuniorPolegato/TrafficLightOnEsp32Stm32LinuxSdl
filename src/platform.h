#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

/* ============================================================
 *  ESP32 (ESP-IDF defines ESP_PLATFORM)
 * ============================================================ */
#if defined(ESP_PLATFORM)
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "freertos/timers.h"
  #include "driver/gpio.h"
  #include "esp_lvgl_port.h"

  #define BTN_GPIO   GPIO_NUM_0          /* BOOT button, active low */

  static inline void platform_button_init(void) {
      gpio_config_t io = {
          .pin_bit_mask = 1ULL << BTN_GPIO,
          .mode         = GPIO_MODE_INPUT,
          .pull_up_en   = GPIO_PULLUP_ENABLE,
      };
      gpio_config(&io);
  }
  static inline bool platform_button_pressed(void) {
      return gpio_get_level(BTN_GPIO) == 0;
  }
  static inline void ui_lock(void)   { lvgl_port_lock(0); }
  static inline void ui_unlock(void) { lvgl_port_unlock(); }
  static inline void platform_lvgl_start(void) { /* esp_lvgl_port already runs LVGL */ }
  static inline bool platform_button2_pressed(void) { return gpio_get_level(GPIO_NUM_35) == 0; }

/* ============================================================
 *  STM32H755 (CubeIDE defines STM32H755xx; use the CM7 core)
 * ============================================================ */
#elif defined(STM32H755xx)
  #include "FreeRTOS.h"
  #include "stm32h7xx_hal.h"
  #include "task.h"
  #include "queue.h"
  #include "timers.h"
  #include "semphr.h"
  #include "lvgl.h"

  /* NUCLEO-H755ZI-Q: user button B1 = PC13, active high */
  #define BTN_PORT        GPIOC
  #define BTN_PIN         GPIO_PIN_13
  #define BTN_ACTIVE_LVL  GPIO_PIN_SET

  extern SemaphoreHandle_t lvgl_mutex;

  static inline void platform_button_init(void) {
      __HAL_RCC_GPIOC_CLK_ENABLE();
      GPIO_InitTypeDef io = {
          .Pin  = BTN_PIN,
          .Mode = GPIO_MODE_INPUT,
          .Pull = GPIO_NOPULL,          /* board has external pull-down */
      };
      HAL_GPIO_Init(BTN_PORT, &io);
  }
  static inline bool platform_button_pressed(void) {
      return HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == BTN_ACTIVE_LVL;
  }
  static inline void ui_lock(void)   { xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY); }
  static inline void ui_unlock(void) { xSemaphoreGiveRecursive(lvgl_mutex); }
  void platform_lvgl_start(void);     /* implemented in main.c */

#elif defined(SIMULATOR)
  #include "FreeRTOS.h"
  #include "task.h"
  #include "queue.h"
  #include "timers.h"
  #include "semphr.h"
  #include "lvgl.h"
  #include <SDL2/SDL.h>

  extern SemaphoreHandle_t lvgl_mutex;
  extern volatile bool sim_btn[2];      /* [0]=GPIO0, [1]=GPIO35 */
  void sim_board_create(void);

  static inline void platform_button_init(void) {}
  static inline bool platform_button_pressed(void) {
      return sim_btn[0] || SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE];
  }
  static inline bool platform_button2_pressed(void) { return sim_btn[1]; }
  static inline void ui_lock(void)   { xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY); }
  static inline void ui_unlock(void) { xSemaphoreGiveRecursive(lvgl_mutex); }
  static inline void platform_lvgl_start(void) {}

#else
  #error "Unsupported platform: define ESP_PLATFORM, STM32H755xx or SIMULATOR"
#endif

#ifndef pdTICKS_TO_MS                 /* older FreeRTOS versions */
  #define pdTICKS_TO_MS(t) ((uint32_t)(((uint64_t)(t) * 1000u) / configTICK_RATE_HZ))
#endif

/* LVGL >= 9.6: per-flag setters */
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
  #define obj_no_scroll(o) lv_obj_set_scrollable((o), false)
#else
  #define obj_no_scroll(o) lv_obj_remove_flag((o), LV_OBJ_FLAG_SCROLLABLE)
#endif

lv_obj_t *platform_ui_root(void);      /* where the app draws */
void      platform_battery_init(void);
uint32_t  platform_battery_mv(void);

#endif /* PLATFORM_H */
