#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Traffic light ---------- */
typedef enum { ST_GREEN, ST_YELLOW, ST_RED, ST_COUNT } state_t;
typedef enum { EV_NONE, EV_TICK, EV_BUTTON } event_t;

typedef struct traffic traffic_t;
typedef state_t (*handler_t)(traffic_t *t, event_t ev);

struct traffic {
    state_t     state;
    uint32_t    ticks;          /* seconds spent in the current state */
    bool        ped_request;    /* pedestrian requested to cross */
    const char *name;
};

void        fsm_init(traffic_t *t, const char *name);
void        fsm_dispatch(traffic_t *t, event_t ev);
const char *fsm_state_str(state_t s);
uint32_t    fsm_remaining(const traffic_t *t);   /* seconds left in the current state */

/* ---------- Debounce ---------- */
typedef enum {
    DB_RELEASED,
    DB_MAYBE_PRESSED,
    DB_PRESSED,
    DB_MAYBE_RELEASED
} db_state_t;

typedef struct {
    db_state_t state;
    uint32_t   t0;          /* time the transition started */
    uint32_t   window_ms;   /* e.g. 50 ms */
} debounce_t;

void    debounce_init(debounce_t *d, uint32_t window_ms);
/* Returns EV_BUTTON once per confirmed press */
event_t debounce_update(debounce_t *d, bool level_pressed, uint32_t now_ms);

#endif
