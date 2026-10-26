#include "app.h"

Theme g_theme;

void theme_init(GRect bounds) {
  g_theme.big = bounds.size.w >= 200;
  g_theme.small = fonts_get_system_font(g_theme.big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14);
  g_theme.lh = g_theme.big ? 20 : 16;
  g_theme.small_bold = fonts_get_system_font(g_theme.big ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);
  g_theme.title = fonts_get_system_font(g_theme.big ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  g_theme.pad_x = g_theme.big ? 6 : 4;
  g_theme.pad_y = 2;
  g_theme.edge = PBL_IF_ROUND_ELSE(bounds.size.w * 8 / 100, 0);
}

// The area that is fully visible: round screens lose their corners.
GRect content_rect(GRect bounds) {
#if defined(PBL_ROUND)
  int ix = bounds.size.w * 12 / 100;
  int iy = bounds.size.h * 9 / 100;
  return grect_inset(bounds, GEdgeInsets(iy, ix));
#else
  return grect_inset(bounds, GEdgeInsets(g_theme.pad_y, g_theme.pad_x));
#endif
}

// A screen-door fill for black-and-white watches: density 2 is every other
// pixel, 4 every fourth.
void fill_dither(GContext *ctx, GRect r, int density) {
  for (int y = r.origin.y; y < r.origin.y + r.size.h; y++) {
    for (int x = r.origin.x; x < r.origin.x + r.size.w; x++) {
      if (density == 2 ? ((x + y) & 1) == 0 : ((x & 1) == 0 && (y & 1) == 0)) {
        graphics_draw_pixel(ctx, GPoint(x, y));
      }
    }
  }
}

// The unlit part of a bar.
void draw_track(GContext *ctx, GRect r) {
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, C_GHOST);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
#else
  graphics_context_set_stroke_color(ctx, GColorWhite);
  fill_dither(ctx, r, 4);
#endif
}

void draw_text(GContext *ctx, const char *s, GFont font, GRect r, GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s, font, r, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Seven segments: a b c d e f g as bits 0..6.
static uint8_t seg_mask(char c) {
  switch (c) {
    case '0': return 0x3f;
    case '1': return 0x06;
    case '2': return 0x5b;
    case '3': return 0x4f;
    case '4': return 0x66;
    case '5': return 0x6d;
    case '6': return 0x7d;
    case '7': return 0x07;
    case '8': return 0x7f;
    case '9': return 0x6f;
    case '-': return 0x40;
    default: return 0;
  }
}

static void seg_rects(GRect cell, int t, GRect out[7]) {
  int x = cell.origin.x, y = cell.origin.y, w = cell.size.w, h = cell.size.h;
  int mid = (h - t) / 2;
  out[0] = GRect(x + t, y, w - 2 * t, t);                         // a
  out[1] = GRect(x + w - t, y + t, t, mid - t);                   // b
  out[2] = GRect(x + w - t, y + mid + t, t, h - mid - 2 * t);     // c
  out[3] = GRect(x + t, y + h - t, w - 2 * t, t);                 // d
  out[4] = GRect(x, y + mid + t, t, h - mid - 2 * t);             // e
  out[5] = GRect(x, y + t, t, mid - t);                           // f
  out[6] = GRect(x + t, y + mid, w - 2 * t, t);                   // g
}

static void seg_metrics(int h, int *dw, int *gap, int *t) {
  *t = h / 9 < 2 ? 2 : h / 9;
  *dw = h * 11 / 20;
  *gap = *t + 1;
}

// Width of `digits` drawn `h` tall, with at least `cells` digit cells.
int segments_width(int h, const char *digits, int cells) {
  int dw, gap, t;
  seg_metrics(h, &dw, &gap, &t);
  int n = 0, dots = 0;
  for (const char *p = digits; *p; p++) {
    if (*p == '.') dots++; else n++;
  }
  if (n < cells) n = cells;
  return n * (dw + gap) + dots * (t + gap) - gap;
}

// A vacuum-fluorescent readout: unlit segments stay faintly visible, and
// spare leading cells show a ghost 8.
void draw_segments(GContext *ctx, GRect box, const char *digits, int cells, bool right_align) {
  draw_segments_color(ctx, box, digits, cells, right_align, C_LIT);
}

void draw_segments_color(GContext *ctx, GRect box, const char *digits, int cells, bool right_align, GColor color) {
  int h = box.size.h, dw, gap, t;
  seg_metrics(h, &dw, &gap, &t);
  int n = 0;
  for (const char *p = digits; *p; p++) if (*p != '.') n++;
  int spare = cells > n ? cells - n : 0;
  int width = segments_width(h, digits, cells);
  int x = right_align ? box.origin.x + box.size.w - width : box.origin.x;
  GRect r[7];

  for (int i = 0; i < spare; i++) {
#if defined(PBL_COLOR)
    seg_rects(GRect(x, box.origin.y, dw, h), t, r);
    graphics_context_set_fill_color(ctx, C_GHOST);
    for (int s = 0; s < 7; s++) graphics_fill_rect(ctx, r[s], 0, GCornerNone);
#endif
    x += dw + gap;
  }
  for (const char *p = digits; *p; p++) {
    if (*p == '.') {
      graphics_context_set_fill_color(ctx, color);
      graphics_fill_rect(ctx, GRect(x, box.origin.y + h - t, t, t), 0, GCornerNone);
      x += t + gap;
      continue;
    }
    uint8_t mask = seg_mask(*p);
    seg_rects(GRect(x, box.origin.y, dw, h), t, r);
    graphics_context_set_fill_color(ctx, color);
    for (int s = 0; s < 7; s++) {
      if (mask & (1 << s)) graphics_fill_rect(ctx, r[s], 0, GCornerNone);
    }
    x += dw + gap;
  }
}

// "At least", drawn, since the system fonts have no ≥.
void draw_geq(GContext *ctx, GPoint at, int size, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  int half = size / 2;
  graphics_draw_line(ctx, at, GPoint(at.x + size, at.y + half));
  graphics_draw_line(ctx, GPoint(at.x + size, at.y + half), GPoint(at.x, at.y + size));
  graphics_draw_line(ctx, GPoint(at.x, at.y + size + 3), GPoint(at.x + size, at.y + size + 3));
}

// The tuner's status column: which line is shown. Black and white draws
// only the lit one.
void draw_indicators(GContext *ctx, GRect r, int line) {
  int lh = r.size.h / 3;
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GColor off = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);
  for (int i = 0; i < 3; i++) {
    draw_text(ctx, line_name(i), f, GRect(r.origin.x, r.origin.y + i * lh - 3, r.size.w, lh + 4), GTextAlignmentRight,
              line == i ? C_LIT : off);
  }
}

// "antistatic." with the red full stop, a counter or status on the right.
// Text wrapped within box; returns the height it took.
int draw_wrapped(GContext *ctx, const char *s, GFont font, GRect box, GTextAlignment align, GColor color) {
  draw_text(ctx, s, font, box, align, color);
  return graphics_text_layout_get_content_size(s, font, box, GTextOverflowModeTrailingEllipsis, align).h;
}

void draw_header(GContext *ctx, GRect c, const char *left, const char *right, bool busy) {
  GFont f = g_theme.small;
  GRect row = GRect(c.origin.x, c.origin.y - 3, c.size.w, g_theme.lh + 4);
#if defined(PBL_ROUND)
  // One centred line on round screens.
  char line[48];
  snprintf(line, sizeof(line), "%s  %s", left, right ? right : "");
  row = grect_inset(row, GEdgeInsets(0, g_theme.edge));
  draw_text(ctx, line, f, row, GTextAlignmentCenter, C_DIM);
#else
  int rw = 0;
  if (right) {
    GSize rs = graphics_text_layout_get_content_size(right, f, row, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
    rw = rs.w + 6;
  }
  draw_text(ctx, left, f, GRect(row.origin.x, row.origin.y, row.size.w - rw, row.size.h), GTextAlignmentLeft, C_DIM);
  if (strcmp(left, "antistatic") == 0) {
    GSize sz = graphics_text_layout_get_content_size(left, f, row, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    graphics_context_set_fill_color(ctx, C_RED);
    graphics_fill_rect(ctx, GRect(c.origin.x + sz.w + 1, c.origin.y + 10, 2, 2), 0, GCornerNone);
  }
  if (right) draw_text(ctx, right, f, row, GTextAlignmentRight, busy ? C_RED : C_DIM);
#endif
}

// Numbers, without floats: v100 is the value times 100.
void fmt_number(char *buf, size_t n, int32_t v100) {
  // As the homepage: whole numbers from 100, one decimal below that, two
  // below 1.
  bool neg = v100 < 0;
  int32_t a = neg ? -v100 : v100;
  const char *sign = neg ? "-" : "";
  if (a >= 10000) {
    snprintf(buf, n, "%s%ld", sign, (long)((a + 50) / 100));
  } else if (a >= 100) {
    int32_t tenths = (a + 5) / 10;
    if (tenths % 10 == 0) snprintf(buf, n, "%s%ld", sign, (long)(tenths / 10));
    else snprintf(buf, n, "%s%ld.%ld", sign, (long)(tenths / 10), (long)(tenths % 10));
  } else if (a == 0) {
    snprintf(buf, n, "0");
  } else if (a % 10 == 0) {
    snprintf(buf, n, "%s0.%ld", sign, (long)(a / 10));
  } else {
    snprintf(buf, n, "%s0.%02ld", sign, (long)a);
  }
}

void fmt_value(char *buf, size_t n, int32_t v100, const Summary *m, bool unit) {
  char num[16];
  fmt_number(num, sizeof(num), v100);
  if (!unit) { snprintf(buf, n, "%s", num); return; }
  if (num[0] == '-' && m->prefix[0]) snprintf(buf, n, "-%s%s%s", m->prefix, num + 1, m->suffix);
  else snprintf(buf, n, "%s%s%s", m->prefix, num, m->suffix);
}

void fmt_prob(char *buf, size_t n, int permille, bool pct) {
  const char *s = pct ? "%" : "";
  if (permille >= 995 && permille < 1000) snprintf(buf, n, "%d.%d%s", permille / 10, permille % 10, s);
  else if (permille < 10) snprintf(buf, n, "%d.%d%s", permille / 10, permille % 10, s);
  else snprintf(buf, n, "%d%s", (permille + 5) / 10, s);
}

// Days since 2000-01-01 to a civil date (Hinnant's algorithm).
static void civil(int32_t day, int *y, int *m, int *d) {
  int32_t z = day + 10957 + 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t yy = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yy + (*m <= 2);
}

int date_year(int32_t day) { int y, m, d; civil(day, &y, &m, &d); return y; }
int date_month(int32_t day) { int y, m, d; civil(day, &y, &m, &d); return m; }

static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void fmt_date(char *buf, size_t n, int32_t day, int cadence) {
  int y, m, d;
  civil(day, &y, &m, &d);
  switch (cadence) {
    case CAD_YEAR: snprintf(buf, n, "%d", y); break;
    case CAD_QUARTER: snprintf(buf, n, "Q%d %d", (m - 1) / 3 + 1, y); break;
    case CAD_MONTH: snprintf(buf, n, "%s %d", MONTHS[m - 1], y); break;
    default: snprintf(buf, n, "%d %s %d", d, MONTHS[m - 1], y); break;
  }
}

// The site's "By end of 2027" / "By Mar 2028".
void fmt_by_date(char *buf, size_t n, int32_t day, int cadence) {
  int y, m, d;
  civil(day, &y, &m, &d);
  if (cadence == CAD_YEAR || (m == 12 && d == 31 && cadence != CAD_DAY && cadence != CAD_WEEK)) {
    snprintf(buf, n, "By end of %d", y);
    return;
  }
  char date[20];
  fmt_date(date, sizeof(date), day, cadence);
  snprintf(buf, n, "By %s", date);
}

// The big tuner line: a small prefix ("By", ≥), segment digits, a unit in
// the MHz slot, and the status column on the right.
void draw_readout(GContext *ctx, GRect r, const char *prefix, bool geq, const char *digits, int cells,
                  const char *unit, int line, bool settled) {
  int iw = g_theme.big ? 54 : 40;
  int pw = (prefix && prefix[0]) || geq ? (g_theme.big ? 24 : 18) : 0;
  GFont uf = g_theme.small_bold;
  int uw = 0;
  if (unit && unit[0]) {
    GSize sz = graphics_text_layout_get_content_size(unit, uf, GRect(0, 0, 80, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    uw = sz.w + 3;
  }
  int avail = r.size.w - iw - pw - uw - 2;
  int h = r.size.h;
  while (h > 12 && segments_width(h, digits, cells) > avail) h -= 2;
  int sw = segments_width(h, digits, cells);
  int base = r.origin.y + r.size.h;
  GRect box = GRect(r.origin.x + pw, base - h, sw, h);
  // Settled bars read in red.
  draw_segments_color(ctx, box, digits, cells, false, settled ? C_RED : C_LIT);

  if (geq) draw_geq(ctx, GPoint(r.origin.x + 2, base - 13), 7, C_DIM);
  else if (pw) draw_text(ctx, prefix, g_theme.small, GRect(r.origin.x, base - g_theme.lh - 1, pw + 4, g_theme.lh + 4), GTextAlignmentLeft, C_DIM);
  if (uw) {
    int fh = g_theme.big ? 22 : 18;
    draw_text(ctx, unit, uf, GRect(box.origin.x + sw + 4, base - fh, uw + 4, fh + 2), GTextAlignmentLeft, C_LIT);
  }
  draw_indicators(ctx, GRect(r.origin.x + r.size.w - iw, r.origin.y, iw, r.size.h), line);
}

// Rows of bars, as on the homepage. Short labels sit to the left of each
// bar; long ones (event names) go above it, with the value beside them.
void rows_layout(RowLayout *L, const Summary *m, int ng, GRect c, int top, int bottom) {
  if (ng < 1) ng = 1;
  int lw = 0;
  for (int g = 0; g < ng; g++) {
    GSize sz = graphics_text_layout_get_content_size(m->groups[g].label, g_theme.small, GRect(0, 0, 300, 20),
                                                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    if (sz.w > lw) lw = sz.w;
  }
  L->vw = g_theme.big ? 52 : 40;
  L->stacked = lw > c.size.w * 2 / 5;
  int h = bottom - top;
  if (L->stacked) {
    L->lw = 0;
    L->rh = h / ng;
    int max_rh = g_theme.big ? 34 : 28;
    if (L->rh > max_rh) L->rh = max_rh;
    if (L->rh < 20) L->rh = 20;
    L->bx = c.origin.x;
    L->bw = c.size.w;
  } else {
    L->lw = lw;
    L->rh = h / ng;
    int max_rh = g_theme.big ? 26 : 20;
    if (L->rh > max_rh) L->rh = max_rh;
    if (L->rh < 10) L->rh = 10;
    L->bx = c.origin.x + (lw ? lw + 4 : 0);
    L->bw = c.origin.x + c.size.w - L->vw - 2 - L->bx;
  }
}

// Draws a row's label and value and returns where its bar goes.
GRect row_draw(GContext *ctx, const RowLayout *L, GRect c, int y, const char *label, const char *value, GColor vcol) {
  int fh = g_theme.big ? 12 : 10;
  if (L->stacked) {
    draw_text(ctx, label, g_theme.small, GRect(c.origin.x, y - 3, c.size.w - L->vw, g_theme.lh + 4), GTextAlignmentLeft, C_TEXT);
    draw_text(ctx, value, g_theme.small_bold, GRect(c.origin.x + c.size.w - L->vw, y - 3, L->vw, 20), GTextAlignmentRight, vcol);
    int top = g_theme.big ? 19 : 14;
    int bh = L->rh - top - 3;
    if (bh < 4) bh = 4;
    if (bh > 10) bh = 10;
    return GRect(L->bx, y + top, L->bw, bh);
  }
  if (L->lw) draw_text(ctx, label, g_theme.small, GRect(c.origin.x, y + L->rh / 2 - fh, L->lw + 2, g_theme.lh + 4), GTextAlignmentLeft, C_TEXT);
  draw_text(ctx, value, g_theme.small_bold, GRect(c.origin.x + c.size.w - L->vw, y + L->rh / 2 - fh, L->vw, 20), GTextAlignmentRight, vcol);
  int bh = L->rh - 6 < 4 ? 4 : L->rh - 6;
  return GRect(L->bx, y + (L->rh - bh) / 2, L->bw, bh);
}

// A row's value: the probability, or how a settled bar came out.
void row_value(char *buf, size_t n, int permille, uint8_t flags, GColor *col, GColor *bar) {
  *col = C_LIT;
  *bar = C_BAR;
  if (flags & PF_RESOLVED) {
    bool yes = flags & PF_YES;
    snprintf(buf, n, yes ? "Yes" : "No");
    *col = yes ? C_RED : C_DIM;
    *bar = yes ? C_RED : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite);
  } else if (permille < 0) {
    snprintf(buf, n, "-");
  } else {
    fmt_prob(buf, n, permille, true);
  }
}
