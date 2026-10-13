#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { int16_t x, y; uint8_t id; } tp_point_t;
typedef struct {
    uint32_t ms;
    uint8_t count;
    tp_point_t p[2];
    bool valid, button;
} tp_input_t;
typedef struct { uint8_t buttons; int16_t dx, dy; int8_t wheel, pan; } tp_report_t;
typedef void (*tp_emit_fn)(const tp_report_t *, void *);
// Tuning in sensor pixels and milliseconds for the 480 x 480 CST9220.
enum {
    TP_HYSTERESIS = 2, TP_TAP_DISTANCE = 10, TP_SCROLL_STEP = 14,
    TP_RELEASE_MS = 24, TP_REJOIN_DISTANCE = 12, TP_JUMP_DISTANCE = 160,
};
typedef struct {
    uint8_t count, buttons, id;
    bool active, multi, moved, dragging, suppress, physical, multi_moved, release_pending;
    int16_t start_x, start_y, last_x, last_y, raw_x, raw_y;
    int16_t scroll_x, scroll_y, unlock_x, unlock_y;
    uint8_t scroll_axes;
    tp_point_t origins[2], previous[2];
    uint32_t down_ms, multi_ms, release_ms;
} tp_state_t;
void tp_reset(tp_state_t *s, tp_emit_fn emit, void *ctx);
void tp_process(tp_state_t *s, const tp_input_t *in, tp_emit_fn emit, void *ctx);
