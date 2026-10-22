/* TrafficLight v0.2.0
 * Tasks, priorities, queues, counting semaphore, mutex, task notifications
 * and a communication gateway task (UART/SPI/CAN).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "platform.h"
#include "fsm.h"

/* ======================= Configuration ======================= */
#define BTN_SAMPLE_MS     5
#define DEBOUNCE_MS       50

#define SAMPLE_HZ         2000                                  /* acquisition rate */
#define SAMPLES_PER_TICK  (SAMPLE_HZ / 1000)                    /* tick = 1 kHz     */
#define PACKET_MS         50
#define PACKET_SAMPLES    (SAMPLE_HZ * PACKET_MS / 1000)        /* 100 samples      */
#define N_PACKETS         4                                     /* buffer pool      */

#define ALARM_ON_MV       3500                                  /* hysteresis */
#define ALARM_OFF_MV      3600

/* Rate monotonic: shorter period -> higher priority */
enum {
    PRIO_UI      = 1,
    PRIO_COMM    = 2,
    PRIO_ACTION  = 2,
    PRIO_DSP     = 3,
    PRIO_FSM     = 3,
    PRIO_TRAFFIC = 3,
    PRIO_BTN     = 4,
    PRIO_ACQ     = 5,
};

/* Notification bits for comm_task */
#define N_STATE  (1u << 0)
#define N_ALARM  (1u << 1)

/* ========================== Types ========================== */
typedef struct {
    uint32_t seq;
    uint16_t mv[PACKET_SAMPLES];
} packet_t;

typedef struct {
    uint32_t seq, dropped;
    uint16_t median, min, max;
} sig_result_t;

typedef struct {
    uint32_t seq;
    uint16_t median;
    bool     alarm;
} sig_status_t;

typedef struct {
    state_t state;
    uint8_t remaining;
    bool    walk;
} traffic_status_t;

typedef enum { UI_TRAFFIC, UI_SIGNAL } ui_kind_t;

typedef struct {
    ui_kind_t kind;
    union { traffic_status_t t; sig_status_t s; };
} ui_msg_t;

/* ======================== Objects ======================== */
static QueueHandle_t     ev_queue;        /* event_t  -> fsm     */
static QueueHandle_t     raw_queue;       /* uint8_t  -> dsp     */
static QueueHandle_t     result_queue;    /* result   -> action  */
static QueueHandle_t     ui_queue;        /* ui_msg_t -> ui      */
static QueueHandle_t     traffic_mbox;    /* 1 slot, overwrite   */
static QueueHandle_t     signal_mbox;     /* 1 slot, overwrite   */
static SemaphoreHandle_t free_slots;      /* counting: free buffers */
static SemaphoreHandle_t log_mutex;       /* protects printf     */
static TaskHandle_t      h_comm;

static packet_t          pool[N_PACKETS];
static volatile uint32_t dropped_packets;
static traffic_t         light;

/* ========================= Logging ========================= */
static void log_printf(const char *fmt, ...)
{
    va_list ap;
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    xSemaphoreGive(log_mutex);
}

/* =========================== UI =========================== */
static const uint32_t color_on[ST_COUNT]  = { 0x00E000, 0xFFC000, 0xFF0000 };
static const uint32_t color_off[ST_COUNT] = { 0x003000, 0x403000, 0x400000 };

static lv_obj_t *lamp[ST_COUNT];
static lv_obj_t *lbl_countdown, *lbl_walk, *lbl_sig;

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

    lamp[ST_RED]    = create_lamp(box, ST_RED);
    lamp[ST_YELLOW] = create_lamp(box, ST_YELLOW);
    lamp[ST_GREEN]  = create_lamp(box, ST_GREEN);

    lbl_countdown = lv_label_create(box);
    lv_obj_set_style_text_color(lbl_countdown, lv_color_white(), 0);
    lv_label_set_text(lbl_countdown, "");

    lbl_walk = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_walk, lv_color_white(), 0);
    lv_obj_align(lbl_walk, LV_ALIGN_BOTTOM_LEFT, 5, -20);
    lv_label_set_text(lbl_walk, "");

    lbl_sig = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_sig, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_sig, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_sig, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_label_set_text(lbl_sig, "-.--V");
}

static void ui_apply(const ui_msg_t *m)
{
    if (m->kind == UI_TRAFFIC) {
        for (int s = 0; s < ST_COUNT; s++)
            lv_obj_set_style_bg_color(lamp[s],
                lv_color_hex(s == (int)m->t.state ? color_on[s] : color_off[s]), 0);
        lv_label_set_text_fmt(lbl_countdown, "%u s", (unsigned)m->t.remaining);
        lv_label_set_text(lbl_walk, m->t.walk ? "W" : "");
    } else {
        lv_label_set_text_fmt(lbl_sig, "%u.%02uV",
                              (unsigned)(m->s.median / 1000),
                              (unsigned)((m->s.median % 1000) / 10));
        lv_obj_set_style_text_color(lbl_sig,
            m->s.alarm ? lv_color_hex(0xFF4040) : lv_color_white(), 0);
    }
}

/* ui_task: the only task that changes widgets (under the LVGL mutex) */
static void ui_task(void *arg)
{
    (void)arg;
    ui_msg_t m;
    for (;;) {
        if (xQueueReceive(ui_queue, &m, portMAX_DELAY) != pdTRUE) continue;
        ui_lock();
        do { ui_apply(&m); } while (xQueueReceive(ui_queue, &m, 0) == pdTRUE);
        ui_unlock();
    }
}

/* ===================== Signal pipeline ===================== */
/* Stage 1: acquisition. In a real product this could be an external
 * chip/co-processor sending raw samples over UART/SPI/I2C/I2S + DMA. */
static void acq_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0;
    uint8_t  idx = 0;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        if (xSemaphoreTake(free_slots, 0) != pdTRUE) {      /* no free buffer */
            dropped_packets++;
            vTaskDelayUntil(&last, pdMS_TO_TICKS(PACKET_MS));
            continue;
        }
        packet_t *p = &pool[idx];
        p->seq = seq++;
        for (int n = 0; n < PACKET_SAMPLES; ) {
            for (int k = 0; k < SAMPLES_PER_TICK && n < PACKET_SAMPLES; k++)
                p->mv[n++] = (uint16_t)platform_signal_read_mv();
            vTaskDelayUntil(&last, 1);                       /* 1 tick = 1 ms */
        }
        xQueueSend(raw_queue, &idx, portMAX_DELAY);          /* len == pool size */
        idx = (uint8_t)((idx + 1) % N_PACKETS);
    }
}

static uint16_t median_sort(uint16_t *a, int n)              /* insertion sort */
{
    for (int i = 1; i < n; i++) {
        uint16_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
    return a[n / 2];
}

/* Stage 2: processing (median here; an FFT would fit the same slot) */
static void dsp_task(void *arg)
{
    (void)arg;
    uint8_t  idx;
    uint16_t buf[PACKET_SAMPLES];

    for (;;) {
        xQueueReceive(raw_queue, &idx, portMAX_DELAY);
        memcpy(buf, pool[idx].mv, sizeof(buf));
        uint32_t seq = pool[idx].seq;
        xSemaphoreGive(free_slots);                          /* release early */

        sig_result_t r = { .seq = seq, .dropped = dropped_packets };
        r.median = median_sort(buf, PACKET_SAMPLES);
        r.min    = buf[0];
        r.max    = buf[PACKET_SAMPLES - 1];
        xQueueSend(result_queue, &r, portMAX_DELAY);
    }
}

/* Stage 3: action (alarm with hysteresis) */
static void action_task(void *arg)
{
    (void)arg;
    sig_result_t r;
    bool alarm = false;

    for (;;) {
        xQueueReceive(result_queue, &r, portMAX_DELAY);
        bool prev = alarm;
        if (!alarm && r.median < ALARM_ON_MV)      alarm = true;
        else if (alarm && r.median > ALARM_OFF_MV) alarm = false;

        sig_status_t s = { .seq = r.seq, .median = r.median, .alarm = alarm };
        xQueueOverwrite(signal_mbox, &s);                    /* latest value */
        if (alarm != prev)
            xTaskNotify(h_comm, N_ALARM, eSetBits);

        ui_msg_t m = { .kind = UI_SIGNAL, .s = s };
        xQueueSend(ui_queue, &m, 0);
    }
}

/* ====================== Traffic light ====================== */
static void traffic_task(void *arg)                          /* replaces the soft timer */
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));         /* no drift */
        event_t ev = EV_TICK;
        xQueueSend(ev_queue, &ev, 0);
    }
}

static void button_task(void *arg)                           /* 5 ms sampling + debounce */
{
    (void)arg;
    debounce_t db;
    debounce_init(&db, DEBOUNCE_MS);
    const TickType_t period = pdMS_TO_TICKS(BTN_SAMPLE_MS) ? pdMS_TO_TICKS(BTN_SAMPLE_MS) : 1;
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

static void publish_traffic(void)
{
    traffic_status_t t = {
        .state = light.state,
        .remaining = (uint8_t)fsm_remaining(&light),
        .walk = light.ped_request,
    };
    xQueueOverwrite(traffic_mbox, &t);
    ui_msg_t m = { .kind = UI_TRAFFIC, .t = t };
    xQueueSend(ui_queue, &m, 0);
}

static void fsm_task(void *arg)                              /* only task that touches the FSM */
{
    (void)arg;
    event_t ev;
    for (;;) {
        if (xQueueReceive(ev_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        state_t old = light.state;

        xSemaphoreTake(log_mutex, portMAX_DELAY);            /* fsm.c uses printf */
        if (ev == EV_BUTTON) printf("  >> Pedestrian pressed\n");
        fsm_dispatch(&light, ev);
        fflush(stdout);
        xSemaphoreGive(log_mutex);

        publish_traffic();
        if (light.state != old)
            xTaskNotify(h_comm, N_STATE, eSetBits);
    }
}

/* ================= Communication gateway ================= */
static void bus_uart_send(const char *line)                  /* stdout = UART on MCUs */
{
    log_printf("%s\r\n", line);
}

static void log_hex(const char *tag, const uint8_t *d, size_t n)
{
    char s[64];
    int p = snprintf(s, sizeof(s), "%s", tag);
    for (size_t i = 0; i < n && p < (int)sizeof(s) - 4; i++)
        p += snprintf(s + p, sizeof(s) - p, " %02X", d[i]);
    log_printf("%s\n", s);
}

static void bus_spi_send(const uint8_t *d, size_t n)         /* stub: e.g. LED expander */
{
    log_hex("[SPI]", d, n);
}

static void bus_can_send(uint16_t id, const uint8_t *d, uint8_t n)  /* stub: TWAI/FDCAN */
{
    char tag[24];
    snprintf(tag, sizeof(tag), "[CAN] 0x%03X [%u]", id, n);
    log_hex(tag, d, n);
}

static void uart_telemetry(const traffic_status_t *t, const sig_status_t *s)
{
    char body[48], line[64];
    snprintf(body, sizeof(body), "TL,%s,%u,%u,%d", fsm_state_str(t->state),
             (unsigned)t->remaining, (unsigned)s->median, s->alarm);
    uint8_t cs = 0;
    for (const char *c = body; *c; c++) cs ^= (uint8_t)*c;   /* NMEA-style XOR */
    snprintf(line, sizeof(line), "$%s*%02X", body, cs);
    bus_uart_send(line);
}

static void comm_task(void *arg)
{
    (void)arg;
    uint32_t hb = 0;
    const TickType_t period = pdMS_TO_TICKS(1000);
    TickType_t next_hb = xTaskGetTickCount() + period;

    for (;;) {
        TickType_t now  = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next_hb - now) > 0 ? next_hb - now : 0;
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);         /* event or heartbeat */

        traffic_status_t t = {0};
        sig_status_t     s = {0};
        xQueuePeek(traffic_mbox, &t, 0);
        xQueuePeek(signal_mbox, &s, 0);

        if (bits & N_STATE) {
            uint8_t spi[2] = { 0xA5, (uint8_t)(1u << t.state) };   /* lamp mask */
            bus_spi_send(spi, sizeof(spi));
            uint8_t can[3] = { (uint8_t)t.state, t.remaining, t.walk };
            bus_can_send(0x200, can, sizeof(can));
        }
        if (bits & N_ALARM) {
            log_printf("[ALARM] %s (median %u mV)\n", s.alarm ? "ON" : "OFF",
                       (unsigned)s.median);
            uint8_t can[3] = { (uint8_t)s.median, (uint8_t)(s.median >> 8), s.alarm };
            bus_can_send(0x300, can, sizeof(can));
        }
        if ((int32_t)(xTaskGetTickCount() - next_hb) >= 0) {
            next_hb += period;
            uart_telemetry(&t, &s);
            uint8_t can[4] = { (uint8_t)hb, (uint8_t)(hb >> 8),
                               (uint8_t)dropped_packets, (uint8_t)(dropped_packets >> 8) };
            bus_can_send(0x7FF, can, sizeof(can));
            hb++;
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
    lv_tick_set_cb(lvgl_tick_cb);
    xTaskCreate(lvgl_task, "lvgl", TASK_STACK(4096), NULL, PRIO_UI, NULL);
}
#endif

/* ================ Shared entry point ================ */
static void app_start(void)
{
    log_mutex = xSemaphoreCreateMutex();

    platform_button_init();
    platform_signal_init();
    platform_lvgl_start();
    fsm_init(&light, "Main Ave.");

    ev_queue     = xQueueCreate(10, sizeof(event_t));
    raw_queue    = xQueueCreate(N_PACKETS, sizeof(uint8_t));
    result_queue = xQueueCreate(4, sizeof(sig_result_t));
    ui_queue     = xQueueCreate(16, sizeof(ui_msg_t));
    traffic_mbox = xQueueCreate(1, sizeof(traffic_status_t));
    signal_mbox  = xQueueCreate(1, sizeof(sig_status_t));
    free_slots   = xSemaphoreCreateCounting(N_PACKETS, N_PACKETS);

    ui_lock();
    ui_init();
    ui_unlock();
    publish_traffic();

    /* comm first: other tasks notify it through h_comm */
    xTaskCreate(comm_task,    "comm",    TASK_STACK(1024), NULL, PRIO_COMM,    &h_comm);
    xTaskCreate(ui_task,      "ui",      TASK_STACK(1024), NULL, PRIO_UI,      NULL);
    xTaskCreate(action_task,  "action",  TASK_STACK(512),  NULL, PRIO_ACTION,  NULL);
    xTaskCreate(dsp_task,     "dsp",     TASK_STACK(1024), NULL, PRIO_DSP,     NULL);
    xTaskCreate(fsm_task,     "fsm",     TASK_STACK(1024), NULL, PRIO_FSM,     NULL);
    xTaskCreate(traffic_task, "traffic", TASK_STACK(512),  NULL, PRIO_TRAFFIC, NULL);
    xTaskCreate(button_task,  "btn",     TASK_STACK(512),  NULL, PRIO_BTN,     NULL);
    xTaskCreate(acq_task,     "acq",     TASK_STACK(512),  NULL, PRIO_ACQ,     NULL);

    log_printf("TrafficLight v%s: %d Hz, %d-sample packets\n",
               APP_VERSION, SAMPLE_HZ, PACKET_SAMPLES);
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

void platform_signal_init(void)
{
    gpio_set_direction(14, GPIO_MODE_OUTPUT);
    gpio_set_level(14, 1);                               /* enable divider */
    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&u, &adc);
    adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &c);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_6,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_create_scheme_curve_fitting(&cc, &cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cc = { .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_create_scheme_line_fitting(&cc, &cali);
#endif
}

uint32_t platform_signal_read_mv(void)                   /* one sample, no averaging */
{
    int raw = 0, mv = 0;
    adc_oneshot_read(adc, ADC_CHANNEL_6, &raw);
    if (cali) adc_cali_raw_to_voltage(cali, raw, &mv); else mv = raw;
    return (uint32_t)mv * 2;                             /* 2:1 divider */
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

void vApplicationTickHook(void) { HAL_IncTick(); }

lv_obj_t *platform_ui_root(void) { return lv_screen_active(); }

void SystemClock_Config(void)
{
#ifdef BOARD_H755
    HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);
#else
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
#endif
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
    /* TODO: PLL / bus clocks (runs on HSI 64 MHz) */
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
#define HSW 1
#define HBP 1
#define VSW 1
#define VBP 1

static uint16_t fb[LCD_W * LCD_H] __attribute__((aligned(32)));

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;              /* DIRECT mode: LVGL already drew into fb */
    lv_display_flush_ready(d);
}

static void display_init(void)
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

    LTDC->SRCR = LTDC_SRCR_IMR;
    LTDC->GCR |= LTDC_GCR_LTDCEN;

    lv_display_t *d = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
}

/* ---------- Signal: ADC3 ch3 on the real board, synthetic in Renode ---------- */
#ifdef BOARD_H755
static ADC_HandleTypeDef hadc3;

void platform_signal_init(void)
{
    __HAL_RCC_ADC3_CLK_ENABLE();
    hadc3.Instance                = ADC3;
    hadc3.Init.Resolution         = ADC_RESOLUTION_16B;
    hadc3.Init.ScanConvMode       = ADC_SCAN_DISABLE;
    hadc3.Init.EOCSelection       = ADC_EOC_SEQ_CONV;
    hadc3.Init.ContinuousConvMode = DISABLE;
    hadc3.Init.NbrOfConversion    = 1;
    hadc3.Init.ExternalTrigConv   = ADC_SOFTWARE_START;
    HAL_ADC_Init(&hadc3);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    ADC_ChannelConfTypeDef c = { .Channel = ADC_CHANNEL_3, .Rank = ADC_REGULAR_RANK_1,
                                 .SamplingTime = ADC_SAMPLETIME_8CYCLES_5,
                                 .SingleDiff = ADC_SINGLE_ENDED };
    HAL_ADC_ConfigChannel(&hadc3, &c);
}

uint32_t platform_signal_read_mv(void)
{
    HAL_ADC_Start(&hadc3);
    if (HAL_ADC_PollForConversion(&hadc3, 1) != HAL_OK) return 0;
    return HAL_ADC_GetValue(&hadc3) * 3300 / 65535 * 2;    /* 2:1 divider */
}
#else
void platform_signal_init(void) {}

/* Triangle 3.3 V..4.1 V over 20 s + noise: the alarm toggles by itself */
uint32_t platform_signal_read_mv(void)
{
    uint32_t t = pdTICKS_TO_MS(xTaskGetTickCount()) % 20000;
    uint32_t base = 3300 + (t < 10000 ? t : 20000 - t) * 800 / 10000;
    return synth_signal_mv(base);
}
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    uart3_init();
    lv_init();
    display_init();
    app_start();
    vTaskStartScheduler();
    for (;;) {}
}

#elif defined(SIMULATOR)

#include <stdlib.h>   /* setenv */

/* SDL must be created and drawn from the same thread -> do it inside the task */
static void boot_task(void *arg)
{
    (void)arg;
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);
    lv_display_t *d = lv_sdl_window_create(280, 268);
    lv_sdl_window_set_zoom(d, 1);
    lv_sdl_mouse_create();
    sim_board_create();
    app_start();
    lvgl_task(NULL);         /* this task becomes the LVGL handler */
}

int main(void)
{
    setenv("SDL_VIDEODRIVER", "x11", 0);
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(boot_task, "lvgl", 8192, NULL, PRIO_UI, NULL);
    vTaskStartScheduler();
    return 0;
}

#endif
