#include <stdio.h>
#include "fsm.h"

/* =====================================================
 *  FSM 1: Traffic light
 * ===================================================== */
#define T_GREEN      15
#define T_GREEN_MIN  3
#define T_YELLOW     5
#define T_RED        20

static state_t on_green(traffic_t *t, event_t ev);
static state_t on_yellow(traffic_t *t, event_t ev);
static state_t on_red(traffic_t *t, event_t ev);

static const handler_t table[ST_COUNT] = {
    [ST_GREEN]  = on_green,
    [ST_YELLOW] = on_yellow,
    [ST_RED]    = on_red,
};

static const char *names[ST_COUNT] = { "GREEN", "YELLOW", "RED" };

const char *fsm_state_str(state_t s) {
    return (s < ST_COUNT) ? names[s] : "?";
}

void fsm_init(traffic_t *t, const char *name) {
    t->state = ST_GREEN;
    t->ticks = 0;
    t->ped_request = false;
    t->name = name;
}

void fsm_dispatch(traffic_t *t, event_t ev) {
    if (ev == EV_NONE) return;
    state_t next = table[t->state](t, ev);
    if (next != t->state) {
        printf("[%s] %s -> %s\n", t->name,
               fsm_state_str(t->state), fsm_state_str(next));
        t->state = next;
        t->ticks = 0;
    }
}

uint32_t fsm_remaining(const traffic_t *t) {
    uint32_t limit;
    switch (t->state) {
    case ST_GREEN:  limit = T_GREEN;  break;
    case ST_YELLOW: limit = T_YELLOW; break;
    default:        limit = T_RED;    break;
    }
    return (t->ticks < limit) ? limit - t->ticks : 1;
}

static state_t on_green(traffic_t *t, event_t ev) {
    if (ev == EV_BUTTON && (T_GREEN - t->ticks) > T_GREEN_MIN) {
        t->ticks = T_GREEN - T_GREEN_MIN;   /* remaining = T_GREEN_MIN */
        t->ped_request = true;
    }
    if (ev == EV_TICK && ++t->ticks >= T_GREEN)
        return ST_YELLOW;
    return ST_GREEN;
}

static state_t on_yellow(traffic_t *t, event_t ev) {
    if (ev == EV_TICK && ++t->ticks >= T_YELLOW)
        return ST_RED;
    return ST_YELLOW;
}

static state_t on_red(traffic_t *t, event_t ev) {
    if (ev == EV_TICK && ++t->ticks >= T_RED) {
        t->ped_request = false;
        return ST_GREEN;
    }
    return ST_RED;
}

/* =====================================================
 *  FSM 2: Debounce
 * ===================================================== */
void debounce_init(debounce_t *d, uint32_t window_ms) {
    d->state = DB_RELEASED;
    d->t0 = 0;
    d->window_ms = window_ms;
}

event_t debounce_update(debounce_t *d, bool pressed, uint32_t now) {
    switch (d->state) {
    case DB_RELEASED:
        if (pressed) { d->state = DB_MAYBE_PRESSED; d->t0 = now; }
        break;

    case DB_MAYBE_PRESSED:
        if (!pressed)
            d->state = DB_RELEASED;                   /* it was noise */
        else if (now - d->t0 >= d->window_ms) {
            d->state = DB_PRESSED;
            return EV_BUTTON;                         /* press confirmed */
        }
        break;

    case DB_PRESSED:
        if (!pressed) { d->state = DB_MAYBE_RELEASED; d->t0 = now; }
        break;

    case DB_MAYBE_RELEASED:
        if (pressed)
            d->state = DB_PRESSED;                    /* it was noise */
        else if (now - d->t0 >= d->window_ms)
            d->state = DB_RELEASED;
        break;
    }
    return EV_NONE;
}
