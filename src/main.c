#include <stdio.h>
#include "platform.h"
#include "fsm.h"

#define SAMPLE_MS    5
#define DEBOUNCE_MS  50

static QueueHandle_t ev_queue;
static traffic_t     light;

static lv_obj_t *lamp[ST_COUNT];
static lv_obj_t *lbl_countdown, *lbl_walk, *lbl_bat;

/* ======================= UI ======================= */
static const uint32_t color_on[ST_COUNT]  = { 0x00E000, 0xFFC000, 0xFF0000 };
static const uint32_t color_off[ST_COUNT] = { 0x003000, 0x403000, 0x400000 };

static lv_obj_t *create_lamp(lv_obj_t *parent, state_t s)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, 50, 50);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color_off[s]), 0);
    obj_no_scroll(o);
    return o;
}

static void ui_init(void)
{
    lv_obj_t *scr = platform_ui_root();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000099), 0);
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 80, 225);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    obj_no_scroll(box);

    lamp[ST_RED]    = create_lamp(box, ST_RED);      /* top-to-bottom order */
    lamp[ST_YELLOW] = create_lamp(box, ST_YELLOW);
    lamp[ST_GREEN]  = create_lamp(box, ST_GREEN);

    lbl_countdown = lv_label_create(box);
    lv_obj_set_style_text_color(lbl_countdown, lv_color_white(), 0);

    lbl_walk = lv_label_create(scr);
    lv_obj_align(lbl_walk, LV_ALIGN_BOTTOM_LEFT, 5, -20);
    lv_label_set_text(lbl_walk, "");

    lbl_bat = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_bat, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_bat, LV_ALIGN_TOP_RIGHT, -2, 2);
}

static void ui_update(void)
{
    ui_lock();
    for (int s = 0; s < ST_COUNT; s++)
        lv_obj_set_style_bg_color(lamp[s],
            lv_color_hex(s == light.state ? color_on[s] : color_off[s]), 0);
    lv_label_set_text_fmt(lbl_countdown, "%lu s", (unsigned long)fsm_remaining(&light));
    lv_label_set_text(lbl_walk, light.ped_request ? "W" : "");
    lv_obj_set_style_text_color(lbl_walk, lv_color_white(), 0);
    uint32_t mv = platform_battery_mv();
    lv_label_set_text_fmt(lbl_bat, "%lu.%luV",
                          (unsigned long)(mv / 1000), (unsigned long)((mv % 1000) / 100));
    ui_unlock();
}

/* ===================== Tasks ===================== */
static void tick_cb(TimerHandle_t tmr)              /* every 1 s */
{
    (void)tmr;
    event_t ev = EV_TICK;
    xQueueSend(ev_queue, &ev, 0);
}

static void button_task(void *arg)                  /* 5 ms sampling + debounce FSM */
{
    (void)arg;
    debounce_t db;
    debounce_init(&db, DEBOUNCE_MS);
    const TickType_t period = pdMS_TO_TICKS(SAMPLE_MS) ? pdMS_TO_TICKS(SAMPLE_MS) : 1;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
        if (debounce_update(&db, platform_button_pressed(), now) == EV_BUTTON) {
            event_t ev = EV_BUTTON;
            xQueueSend(ev_queue, &ev, 0);
        }
        vTaskDelayUntil(&last, period);
    }
}

static void fsm_task(void *arg)                     /* only task that touches the FSM */
{
    (void)arg;
    event_t ev;
    ui_update();
    for (;;) {
        if (xQueueReceive(ev_queue, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev == EV_BUTTON) printf("  >> Pedestrian pressed\n");
            fsm_dispatch(&light, ev);
            ui_update();
        }
    }
}

/* ============ STM32 + simulator: LVGL handler task ============ */
#if defined(STM32H755xx) || defined(SIMULATOR)
SemaphoreHandle_t lvgl_mutex;

static uint32_t lvgl_tick_cb(void) { return pdTICKS_TO_MS(xTaskGetTickCount()); }

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        ui_lock();
        uint32_t wait = lv_timer_handler();
        ui_unlock();
        vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 5 : (wait > 30 ? 30 : wait)));
    }
}
#endif

#if defined(STM32H755xx)
void platform_lvgl_start(void)
{
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    lv_tick_set_cb(lvgl_tick_cb);                   /* LVGL v9; v8: call lv_tick_inc() */
    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 4, NULL);
}
#endif

/* ================ Shared entry point ================ */
static void app_start(void)
{
    platform_button_init();
    platform_battery_init();
    platform_lvgl_start();
    fsm_init(&light, "Main Ave.");

    ui_lock();
    ui_init();
    ui_unlock();

    ev_queue = xQueueCreate(10, sizeof(event_t));

    xTaskCreate(fsm_task,    "fsm", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "btn", 2048, NULL, 6, NULL);  /* higher priority: precise sampling */
    xTimerStart(xTimerCreate("tick", pdMS_TO_TICKS(1000), pdTRUE, NULL, tick_cb), 0);
}

#if defined(ESP_PLATFORM)
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"

#define PIN_MOSI  19
#define PIN_SCLK  18
#define PIN_CS    5
#define PIN_DC    16
#define PIN_RST   23
#define PIN_BL    4
#define LCD_H_RES 135
#define LCD_V_RES 240

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;

lv_obj_t *platform_ui_root(void) { return lv_screen_active(); }

void platform_battery_init(void)
{
    gpio_set_direction(14, GPIO_MODE_OUTPUT);
    gpio_set_level(14, 1);                               /* enable divider */
    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&u, &adc);
    adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &c);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED          /* ESP32-S3, C3, C6... */
    adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_6,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_create_scheme_curve_fitting(&cc, &cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED         /* ESP32 (T-Display) */
    adc_cali_line_fitting_config_t cc = { .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_create_scheme_line_fitting(&cc, &cali);
#endif
}

uint32_t platform_battery_mv(void)
{
    int raw, mv, sum = 0;
    for (int i = 0; i < 16; i++) {
        adc_oneshot_read(adc, ADC_CHANNEL_6, &raw);
        if (cali) adc_cali_raw_to_voltage(cali, raw, &mv); else mv = raw;
        sum += mv;
    }
    return (uint32_t)(sum / 16) * 2;                     /* 2:1 divider */
}

static void lcd_init(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel)
{
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .sclk_io_num = PIN_SCLK,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC,
        .spi_mode = 0, .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, io));

    esp_lcd_panel_dev_config_t p_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(*io, &p_cfg, panel));
    esp_lcd_panel_reset(*panel);
    esp_lcd_panel_init(*panel);
    esp_lcd_panel_invert_color(*panel, true);
    esp_lcd_panel_set_gap(*panel, 52, 40);
    esp_lcd_panel_disp_on_off(*panel, true);
}

static void lvgl_init(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io, .panel_handle = panel,
        .buffer_size = LCD_H_RES * 40, .double_buffer = true,
        .hres = LCD_H_RES, .vres = LCD_V_RES,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    lvgl_port_add_disp(&disp_cfg);
}

void app_main(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    printf("ESP32-S3: build-only target, no display\n");
#else
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    lcd_init(&io, &panel);
    lvgl_init(io, panel);
    app_start();
#endif
}

#elif defined(STM32H755xx)
/* Call from the CM7 main() generated by CubeMX:
 *   HAL_Init(); SystemClock_Config(); MX_..._Init();
 *   lv_init(); display_init();      // your LTDC/SPI display driver
 *   app_start_stm32();
 *   vTaskStartScheduler();
 */
void app_start_stm32(void) { app_start(); }

/* ---------- FreeRTOS hooks ---------- */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name;
    __disable_irq();
    for (;;) {}            /* set a breakpoint here */
}

void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    for (;;) {}
}

/* ---------- Platform stubs ---------- */
lv_obj_t *platform_ui_root(void) { return lv_screen_active(); }

void vApplicationTickHook(void) { HAL_IncTick(); }

void SystemClock_Config(void)
{
    // RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    // RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* 1. Supply config: must come FIRST */
#ifdef BOARD_H755
    HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);
#else
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
#endif

    /* 2. Voltage scaling */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    /* 3. Oscillators / PLL / bus clocks... */
}

static UART_HandleTypeDef huart3;

static void uart3_init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef g = { .Pin = GPIO_PIN_8 | GPIO_PIN_9, .Mode = GPIO_MODE_AF_PP,
                           .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = GPIO_AF7_USART3 };
    HAL_GPIO_Init(GPIOD, &g);
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&huart3);
}

int _write(int fd, char *p, int len)
{
    (void)fd;
    HAL_UART_Transmit(&huart3, (uint8_t *)p, len, HAL_MAX_DELAY);
    return len;
}

#define LCD_W 135
#define LCD_H 240
#define HSW 1   /* hsync width */
#define HBP 1   /* h back porch */
#define VSW 1
#define VBP 1

static uint16_t fb[LCD_W * LCD_H] __attribute__((aligned(32)));

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;              /* DIRECT mode: LVGL already drew into fb */
    lv_display_flush_ready(d);
}

void display_init(void)
{
    __HAL_RCC_LTDC_CLK_ENABLE();

    LTDC->SSCR = ((HSW - 1) << 16) | (VSW - 1);
    LTDC->BPCR = ((HSW + HBP - 1) << 16) | (VSW + VBP - 1);
    LTDC->AWCR = ((HSW + HBP + LCD_W - 1) << 16) | (VSW + VBP + LCD_H - 1);
    LTDC->TWCR = ((HSW + HBP + LCD_W) << 16) | (VSW + VBP + LCD_H);

    LTDC_Layer1->WHPCR  = ((HSW + HBP + LCD_W - 1) << 16) | (HSW + HBP);
    LTDC_Layer1->WVPCR  = ((VSW + VBP + LCD_H - 1) << 16) | (VSW + VBP);
    LTDC_Layer1->PFCR   = 2;                                   /* RGB565 */
    LTDC_Layer1->CFBAR  = (uint32_t)fb;
    LTDC_Layer1->CFBLR  = ((LCD_W * 2) << 16) | (LCD_W * 2 + 7);
    LTDC_Layer1->CFBLNR = LCD_H;
    LTDC_Layer1->CACR   = 255;
    LTDC_Layer1->CR     = LTDC_LxCR_LEN;

    LTDC->SRCR = LTDC_SRCR_IMR;                                /* reload */
    LTDC->GCR |= LTDC_GCR_LTDCEN;

    lv_display_t *d = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
}

/* ---------- Battery (ADC3, channel 3) ---------- */
static ADC_HandleTypeDef hadc3;

void platform_battery_init(void)
{
    __HAL_RCC_ADC3_CLK_ENABLE();
    hadc3.Instance                   = ADC3;
    hadc3.Init.Resolution            = ADC_RESOLUTION_16B;
    hadc3.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc3.Init.EOCSelection          = ADC_EOC_SEQ_CONV;   /* Renode only sets EOS */
    hadc3.Init.ContinuousConvMode    = DISABLE;
    hadc3.Init.NbrOfConversion       = 1;
    hadc3.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    HAL_ADC_Init(&hadc3);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    ADC_ChannelConfTypeDef c = { .Channel = ADC_CHANNEL_3, .Rank = ADC_REGULAR_RANK_1,
                                 .SamplingTime = ADC_SAMPLETIME_8CYCLES_5,
                                 .SingleDiff = ADC_SINGLE_ENDED };
    HAL_ADC_ConfigChannel(&hadc3, &c);
}

uint32_t platform_battery_mv(void)
{
    HAL_ADC_Start(&hadc3);
    if (HAL_ADC_PollForConversion(&hadc3, 10) != HAL_OK) return 0;
    uint32_t raw = HAL_ADC_GetValue(&hadc3);
    return raw * 3300 / 65535 * 2;     /* 2:1 divider, same as ESP32 */
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    uart3_init();
    lv_init();
    display_init();        /* your LTDC/SPI driver, when available */
    app_start_stm32();     /* calls platform_button_init / platform_battery_init */
    vTaskStartScheduler();
    for (;;) {}
}


#elif defined(SIMULATOR)

#include <stdlib.h>   /* setenv */

/* SDL must be created and drawn from the same thread → do it inside the task */
static void boot_task(void *arg)
{
    (void)arg;
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);
    lv_display_t *d = lv_sdl_window_create(280, 268);
    lv_sdl_window_set_zoom(d, 1);
    lv_sdl_mouse_create();
    sim_board_create();      /* LVGL thread, before other tasks exist */
    app_start();             /* same tasks/queue/timer as on the ESP32 */
    lvgl_task(NULL);         /* this task becomes the LVGL handler */
}

int main(void)
{
    setenv("SDL_VIDEODRIVER", "x11", 0);   /* 0 = keep it if the user already set it */
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(boot_task, "lvgl", 8192, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}

#endif
