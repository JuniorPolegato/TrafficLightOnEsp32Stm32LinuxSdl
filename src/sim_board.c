#if defined(SIMULATOR)
#include "platform.h"

#define SCR_W 135
#define SCR_H 240

volatile bool sim_btn[2];
static volatile int bat_mv = 3900;
static lv_obj_t *root, *lbl_bat;

static void btn_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) sim_btn[id] = true;
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) sim_btn[id] = false;
}

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    bat_mv = lv_slider_get_value(s);
    lv_label_set_text_fmt(lbl_bat, "%d.%02dV", bat_mv / 1000, (bat_mv % 1000) / 10);
}

static void make_btn(lv_obj_t *p, const char *txt, int id, int y)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 90, 36);
    lv_obj_set_pos(b, 175, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x505050), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_ALL, (void *)(intptr_t)id);
}

void sim_board_create(void)
{
    lv_obj_t *board = lv_screen_active();
    lv_obj_set_style_bg_color(board, lv_color_black(), 0);
    obj_no_scroll(board);

    /* bezel + T-Display screen */
    lv_obj_t *bezel = lv_obj_create(board);
    lv_obj_set_size(bezel, SCR_W + 12, SCR_H + 12);
    lv_obj_set_pos(bezel, 14, 8);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(bezel, 0, 0);
    lv_obj_set_style_radius(bezel, 6, 0);
    lv_obj_set_style_pad_all(bezel, 6, 0);
    obj_no_scroll(bezel);

    root = lv_obj_create(bezel);
    lv_obj_remove_style_all(root);          /* clean 135x240 area, clips children */
    lv_obj_set_size(root, SCR_W, SCR_H);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    obj_no_scroll(root);

    /* buttons */
    make_btn(board, "GPIO0",  0, 14);
    make_btn(board, "GPIO35", 1, 60);

    /* battery slider */
    lv_obj_t *s = lv_slider_create(board);
    lv_obj_set_size(s, 16, 110);            /* height > width -> vertical */
    lv_obj_set_pos(s, 212, 122);
    lv_slider_set_range(s, 3000, 4200);
    lv_slider_set_value(s, bat_mv, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_bat = lv_label_create(board);
    lv_obj_set_style_text_color(lbl_bat, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_bat, 200, 242);
    lv_obj_send_event(s, LV_EVENT_VALUE_CHANGED, NULL);   /* initial text */
}

lv_obj_t *platform_ui_root(void)   { return root; }
void      platform_battery_init(void) {}
uint32_t  platform_battery_mv(void) { return (uint32_t)bat_mv; }

#endif
