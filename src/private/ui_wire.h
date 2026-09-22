/* Private wire v1. Keep identical to display_ui/src/app/extension_wire.h. */
#ifndef PILOT_UI_WIRE_H
#define PILOT_UI_WIRE_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EU_NAME "tech.andless.Display"
#define EU_PATH "/tech/andless/Display"
#define EU_IFACE "tech.andless.Display.UI1"
#define EU_MAX 8192
#define EU_CONTROLS 32
#define EU_PAGES 8
#define EU_TEXT 128
#define EU_TITLE 48
#define EU_VERSION 1
enum { EU_REGISTER = 1, EU_PATCH, EU_SHOW, EU_HIDE, EU_REMOVE, EU_PING, EU_ACK };
enum { EU_OK, EU_INVALID, EU_BUSY, EU_STALE, EU_DENIED, EU_MISSING };
enum { EU_READY = 1, EU_ACTION, EU_RESULT, EU_CLOSED, EU_RESET, EU_ERROR };
typedef struct {
    uint32_t id, kind;
    char label[EU_TITLE], text[EU_TEXT];
    double value, minimum, maximum, step;
    uint8_t enabled;
} eu_control;
typedef struct {
    uint8_t data[EU_MAX];
    size_t size, pos;
    int bad;
} eu_wire;
static inline void eu_u32(eu_wire *w, uint32_t v) {
    if (w->size + 4 > EU_MAX) {
        w->bad = 1;
        return;
    }
    for (unsigned i = 0; i < 4; i++)
        w->data[w->size++] = (uint8_t)(v >> (8 * i));
}
static inline uint32_t eu_r32(eu_wire *w) {
    if (w->pos + 4 > w->size) {
        w->bad = 1;
        return 0;
    }
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++)
        v |= (uint32_t)w->data[w->pos++] << (8 * i);
    return v;
}
static inline void eu_u64(eu_wire *w, uint64_t v) {
    eu_u32(w, (uint32_t)v);
    eu_u32(w, (uint32_t)(v >> 32));
}
static inline uint64_t eu_r64(eu_wire *w) {
    uint64_t lo = eu_r32(w);
    return lo | ((uint64_t)eu_r32(w) << 32);
}
static inline void eu_double(eu_wire *w, double v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    eu_u64(w, b);
}
static inline double eu_rd(eu_wire *w) {
    uint64_t b = eu_r64(w);
    double v;
    memcpy(&v, &b, 8);
    if (!isfinite(v))
        w->bad = 1;
    return v;
}
static inline int eu_string_ok(const char *s, size_t cap) {
    if (!s)
        return 0;
    size_t n = 0;
    while (n < cap && s[n])
        n++;
    if (n == cap)
        return 0;
    /* UTF-8 without control characters, overlong forms or surrogate codepoints. */
    for (size_t i = 0; i < n;) {
        unsigned c = (unsigned char)s[i++], k = 0, cp = c, min = 0;
        if (c < 32 || c == 127)
            return 0;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            k = 1;
            cp = c & 31;
            min = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            k = 2;
            cp = c & 15;
            min = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            k = 3;
            cp = c & 7;
            min = 65536;
        } else
            return 0;
        while (k--) {
            if (i >= n || ((unsigned char)s[i] & 0xc0) != 0x80)
                return 0;
            cp = (cp << 6) | ((unsigned char)s[i++] & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return 0;
    }
    return 1;
}
static inline void eu_str(eu_wire *w, const char *s) {
    size_t n = strlen(s);
    eu_u32(w, (uint32_t)n);
    if (w->size + n > EU_MAX) {
        w->bad = 1;
        return;
    }
    memcpy(w->data + w->size, s, n);
    w->size += n;
}
static inline void eu_rs(eu_wire *w, char *s, size_t cap) {
    uint32_t n = eu_r32(w);
    if (n >= cap || n > w->size - w->pos) {
        w->bad = 1;
        s[0] = 0;
        return;
    }
    memcpy(s, w->data + w->pos, n);
    s[n] = 0;
    w->pos += n;
    if (strlen(s) != n || !eu_string_ok(s, cap))
        w->bad = 1;
}
static inline int eu_valid_control(const eu_control *c) {
    return c->id && c->kind >= 1 && c->kind <= 5 && c->enabled <= 1 &&
           eu_string_ok(c->label, EU_TITLE) && eu_string_ok(c->text, EU_TEXT) &&
           isfinite(c->value) && isfinite(c->minimum) && isfinite(c->maximum) &&
           isfinite(c->step) && fabs(c->value) <= 1e12 && fabs(c->minimum) <= 1e12 &&
           fabs(c->maximum) <= 1e12 && c->step <= 1e12 && c->minimum <= c->maximum &&
           c->value >= c->minimum && c->value <= c->maximum && c->step >= 0 &&
           (c->kind != 2 || c->step > 0) &&
           (c->kind != 4 ||
            (c->minimum == 0 && c->maximum == 1 && (c->value == 0 || c->value == 1)));
}
static inline void eu_put_control(eu_wire *w, const eu_control *c) {
    eu_u32(w, c->id);
    eu_u32(w, c->kind);
    eu_str(w, c->label);
    eu_str(w, c->text);
    eu_double(w, c->value);
    eu_double(w, c->minimum);
    eu_double(w, c->maximum);
    eu_double(w, c->step);
    eu_u32(w, c->enabled);
}
static inline void eu_get_control(eu_wire *w, eu_control *c) {
    memset(c, 0, sizeof(*c));
    c->id = eu_r32(w);
    c->kind = eu_r32(w);
    eu_rs(w, c->label, sizeof(c->label));
    eu_rs(w, c->text, sizeof(c->text));
    c->value = eu_rd(w);
    c->minimum = eu_rd(w);
    c->maximum = eu_rd(w);
    c->step = eu_rd(w);
    uint32_t e = eu_r32(w);
    c->enabled = (uint8_t)e;
    if (e > 1 || !eu_valid_control(c))
        w->bad = 1;
}
static inline void eu_header(eu_wire *w, unsigned op, uint64_t epoch, uint32_t page,
                             uint64_t revision) {
    memset(w, 0, sizeof(*w));
    eu_u32(w, EU_VERSION);
    eu_u32(w, op);
    eu_u64(w, epoch);
    eu_u32(w, page);
    eu_u64(w, revision);
}
#endif
