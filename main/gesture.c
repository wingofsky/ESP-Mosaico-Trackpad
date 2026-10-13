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

// Moving deadband: small changes accumulate against a stable anchor, so slow
// deliberate motion survives. Unlike a time filter, it never coasts after stop.
static int hysteresis(int value, int anchor)
{
    int delta = value - anchor;
    if (delta > TP_HYSTERESIS) return value - TP_HYSTERESIS;
    if (delta < -TP_HYSTERESIS) return value + TP_HYSTERESIS;
    return anchor;
}

static const tp_point_t *find_point(const tp_point_t *points, unsigned count, uint8_t id)
{
    for (unsigned i = 0; i < count; i++) if (points[i].id == id) return &points[i];
    return NULL;
}

static bool near(const tp_point_t *a, const tp_point_t *b, int distance)
{
    return a && b && abs(a->x - b->x) <= distance && abs(a->y - b->y) <= distance;
}

static void finish_touch(tp_state_t *s, uint32_t up_ms, tp_emit_fn emit, void *ctx)
{
    if (s->active && !s->suppress && !s->physical) {
        bool tap = !s->moved && !s->dragging && up_ms - s->down_ms <= 260;
        bool right = s->multi && !s->multi_moved && up_ms - s->multi_ms <= 300;
        if ((tap && !s->multi) || right) {
            send(s, emit, ctx, right ? 2 : 1, 0, 0, 0, 0);
            send(s, emit, ctx, 0, 0, 0, 0, 0);
        }
    }
    send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, 0, 0);
    bool physical = s->physical;
    uint8_t buttons = s->buttons;
    memset(s, 0, sizeof(*s));
    s->physical = physical;
    s->buttons = buttons;
}

// Start on the dominant axis; allow intentional diagonals and later turns.
// Discard minor-axis buildup during straight travel, not deliberate slow scroll.
static void scroll(tp_state_t *s, int dx, int dy, tp_emit_fn emit, void *ctx)
{
    if (!s->scroll_axes) {
        s->scroll_x += dx;
        s->scroll_y += dy;
        int ax = abs(s->scroll_x), ay = abs(s->scroll_y);
        if (ax < TP_SCROLL_STEP && ay < TP_SCROLL_STEP) return;
        s->scroll_axes = ax > ay * 2 ? 1 : ay > ax * 2 ? 2 : 3;
        if (!(s->scroll_axes & 1)) s->scroll_x = 0;
        if (!(s->scroll_axes & 2)) s->scroll_y = 0;
    } else {
        if (s->scroll_axes & 1) s->scroll_x += dx;
        if (s->scroll_axes & 2) s->scroll_y += dy;
        if (s->scroll_axes != 3) {
            s->unlock_x += dx;
            s->unlock_y += dy;
            int minor = s->scroll_axes == 1 ? s->unlock_y : s->unlock_x;
            int major = s->scroll_axes == 1 ? s->unlock_x : s->unlock_y;
            if (abs(minor) >= TP_SCROLL_STEP && abs(minor) * 2 >= abs(major)) {
                if (s->scroll_axes == 1) s->scroll_y = minor;
                else s->scroll_x = minor;
                s->scroll_axes = 3;
            } else if (abs(major) >= TP_SCROLL_STEP && abs(major) > abs(minor) * 2) {
                s->unlock_x = s->unlock_y = 0;
            }
        }
    }
    int pan = s->scroll_x / TP_SCROLL_STEP;
    int wheel = -s->scroll_y / TP_SCROLL_STEP;
    // Carry the remainder across reports, including direction reversals.
    s->scroll_x -= pan * TP_SCROLL_STEP;
    s->scroll_y += wheel * TP_SCROLL_STEP;
    if (wheel || pan) s->multi_moved = true;
    send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, wheel, pan);
}

void tp_process(tp_state_t *s, const tp_input_t *in, tp_emit_fn emit, void *ctx)
{
    bool valid = in->valid && in->count <= 2;
    for (unsigned i = 0; valid && i < in->count; i++)
        valid = in->p[i].x >= 0 && in->p[i].x < 480 && in->p[i].y >= 0 && in->p[i].y < 480;
    if (valid && in->count == 2 && in->p[0].id == in->p[1].id) valid = false;
    if (!valid) {
        // Sensor faults and disconnects bypass release debounce.
        tp_reset(s, emit, ctx);
        s->suppress = true;
        return;
    }
    if (s->physical != in->button) {
        s->physical = in->button;
        if (s->active) s->moved = s->multi_moved = true;
        send(s, emit, ctx, (s->physical || s->dragging) ? 1 : 0, 0, 0, 0, 0);
    }
    if (!in->count) {
        if (s->active && !s->suppress) {
            if (!s->release_pending) { s->release_pending = true; s->release_ms = in->ms; }
            if (in->ms - s->release_ms < TP_RELEASE_MS) return;
        }
        finish_touch(s, s->release_pending ? s->release_ms : in->ms, emit, ctx);
        return;
    }
    if (s->release_pending) {
        bool rejoin = in->ms - s->release_ms < TP_RELEASE_MS && in->count == s->count;
        for (unsigned i = 0; rejoin && i < in->count; i++)
            rejoin = near(&in->p[i], find_point(s->previous, s->count, in->p[i].id), TP_REJOIN_DISTANCE);
        if (!rejoin) finish_touch(s, s->release_ms, emit, ctx);
        s->release_pending = false;
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
        s->start_x = s->last_x = s->raw_x = x;
        s->start_y = s->last_y = s->raw_y = y;
        s->id = in->p[0].id;
        s->moved = s->physical;
    }
    bool entering_multi = in->count == 2 && !s->multi;
    if (entering_multi) {
        s->multi = true;
        s->multi_ms = in->ms;
        memcpy(s->origins, in->p, sizeof(s->origins));
        s->last_x = s->raw_x = x;
        s->last_y = s->raw_y = y;
        s->multi_moved = s->moved || s->dragging || s->physical;
        s->dragging = false;
        send(s, emit, ctx, s->physical ? 1 : 0, 0, 0, 0, 0);
    }
    if (s->multi) {
        // Each finger must stay still for a right tap. Opposite finger motion
        // can leave the centroid unchanged and must not become a right click.
        for (unsigned i = 0; i < in->count; i++)
            if (!near(&in->p[i], find_point(s->origins, 2, in->p[i].id), TP_TAP_DISTANCE))
                s->multi_moved = true;
        if (in->count == 1) {
            s->count = 1;
            s->previous[0] = in->p[0];
            return; // No pointer jump after two-finger scroll.
        }
    }
    bool rebase = (s->multi && s->count == 1 && !entering_multi) || abs(x - s->raw_x) > TP_JUMP_DISTANCE ||
                  abs(y - s->raw_y) > TP_JUMP_DISTANCE;
    if (s->count == in->count) {
        for (unsigned i = 0; i < in->count; i++) {
            const tp_point_t *old = find_point(s->previous, s->count, in->p[i].id);
            if (!old && !s->multi) {
                tp_reset(s, emit, ctx); s->suppress = true; return;
            }
            if (!near(&in->p[i], old, TP_JUMP_DISTANCE)) rebase = true;
        }
    }
    if (rebase) {
        s->moved = s->multi_moved = true;
        s->last_x = x; s->last_y = y;
        s->scroll_x = s->scroll_y = s->unlock_x = s->unlock_y = 0;
    }
    int filtered_x = hysteresis(x, s->last_x), filtered_y = hysteresis(y, s->last_y);
    int dx = filtered_x - s->last_x, dy = filtered_y - s->last_y;
    if (in->count == 2) {
        scroll(s, dx, dy, emit, ctx);
    } else {
        if (abs(x - s->start_x) > TP_TAP_DISTANCE || abs(y - s->start_y) > TP_TAP_DISTANCE)
            s->moved = true;
        if (!s->moved && in->ms - s->down_ms >= 420) s->dragging = true;
        // macOS still owns pointer acceleration; preserve the established gain.
        send(s, emit, ctx, (s->physical || s->dragging) ? 1 : 0, dx * 2, dy * 2, 0, 0);
    }
    s->last_x = filtered_x; s->last_y = filtered_y;
    s->raw_x = x; s->raw_y = y;
    s->count = in->count;
    memcpy(s->previous, in->p, sizeof(s->previous));
}
