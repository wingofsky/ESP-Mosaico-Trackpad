#include "gesture.h"
#include <stdlib.h>
#include <string.h>

static void send(tp_state_t *s, tp_emit_fn emit, void *ctx,
                 uint8_t buttons, int dx, int dy, int wheel, int pan)
{
    if (buttons == s->buttons && !dx && !dy && !wheel && !pan) return;
    tp_report_t r = {buttons, dx, dy, wheel, pan};
    emit(&r, ctx);
    s->buttons = buttons;
}

void tp_reset(tp_state_t *s, tp_emit_fn emit, void *ctx)
{
    send(s, emit, ctx, 0, 0, 0, 0, 0);
    memset(s, 0, sizeof(*s));
}

void tp_process(tp_state_t *s, const tp_input_t *in, tp_emit_fn emit, void *ctx)
{
    if (!in->valid || in->count > 2) {
        // A sensor error must never become a tap or leave a button held.
        tp_reset(s, emit, ctx);
        s->suppress = true;
        return;
    }
    if (s->physical != in->button) {
        s->physical = in->button;
        if (s->active) s->moved = true; // Button click and tap are independent.
        send(s, emit, ctx, (s->physical || s->dragging) ? 1 : 0, 0, 0, 0, 0);
    }
    if (!in->count) {
        if (s->active && !s->suppress && !s->physical) {
            bool tap = !s->moved && !s->dragging && in->ms - s->down_ms <= 260;
            bool right = s->multi && !s->multi_moved && in->ms - s->multi_ms <= 300;
            if ((tap && !s->multi) || right) {
                uint8_t b = right ? 2 : 1;
                send(s, emit, ctx, b, 0, 0, 0, 0);
                send(s, emit, ctx, 0, 0, 0, 0, 0);
            }
        }
        send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, 0, 0);
        bool physical = s->physical;
        uint8_t buttons = s->buttons;
        memset(s, 0, sizeof(*s));
        s->physical = physical;
        s->buttons = buttons;
        return;
    }
    if (s->suppress) return;
    int x = in->p[0].x, y = in->p[0].y;
    if (in->count == 2) {
        x = (x + in->p[1].x) / 2;
        y = (y + in->p[1].y) / 2;
    }
    if (!s->active) {
        s->active = true;
        s->down_ms = in->ms;
        s->start_x = s->last_x = x;
        s->start_y = s->last_y = y;
        s->id = in->p[0].id;
        s->moved = s->physical;
    }
    if (in->count == 2 && s->count != 2) {
        s->multi = true;
        s->multi_ms = in->ms;
        s->multi_x = s->last_x = x;
        s->multi_y = s->last_y = y;
        s->multi_moved = s->moved || s->dragging || s->physical;
        s->dragging = false;
        s->scroll_x = s->scroll_y = 0;
        send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, 0, 0);
    }
    if (s->multi && in->count == 1) {
        // Wait for complete release after a two-finger gesture. No cursor jump.
        s->count = 1;
        return;
    }
    int dx = x - s->last_x, dy = y - s->last_y;
    if (abs(dx) > 160 || abs(dy) > 160 ||
        (in->count == 1 && s->count == 1 && s->id != in->p[0].id)) {
        s->moved = s->multi_moved = true;
        dx = dy = 0; // Rebase on contact replacement or an implausible jump.
    }
    if (in->count == 2) {
        if (abs(x - s->multi_x) > 10 || abs(y - s->multi_y) > 10)
            s->multi_moved = true;
        s->scroll_x += dx;
        s->scroll_y += dy;
        int pan = s->scroll_x / 14, wheel = -s->scroll_y / 14;
        s->scroll_x -= pan * 14;
        s->scroll_y += wheel * 14;
        if (wheel || pan) s->multi_moved = true;
        send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, wheel, pan);
    } else {
        if (abs(x - s->start_x) > 10 || abs(y - s->start_y) > 10) s->moved = true;
        if (!s->moved && in->ms - s->down_ms >= 420) s->dragging = true;
        send(s, emit, ctx, (s->physical || s->dragging) ? 1 : 0, dx * 2, dy * 2, 0, 0);
    }
    s->last_x = x;
    s->last_y = y;
    s->id = in->p[0].id;
    s->count = in->count;
}
