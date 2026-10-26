#include "app.h"

// Level 3: one curve as a point chart, every bar with a tick for the other
// line.
// Up and Down step bar by bar; Select moves to the next curve.

static Window *s_window;
static Layer *s_layer;
static int s_market, s_group, s_idx;

static const Summary *market(void) { return &g_markets[s_market]; }

static int shown(const Point *p) { return line_value(p, g_line); }
static int other(const Point *p) { return line_value(p, other_line(g_line)); }

static void bar_label(char *buf, size_t n, const Point *p) {
  const Summary *m = market();
  if (m->kind == KIND_COUNT) {
    char v[20];
    fmt_value(v, sizeof(v), p->x, m, true);
    snprintf(buf, n, "%s", v);
  } else {
    fmt_by_date(buf, n, p->x, m->cadence);
  }
}

static void update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  GRect c = content_rect(bounds);
  const Summary *m = market();
  bool count = m->kind == KIND_COUNT;

  const Point *pts;
  int n = group_points(s_group, &pts);
  if (n == 0) return;
  if (s_idx >= n) s_idx = n - 1;
  const Point *p = &pts[s_idx];

  // The market's whole title, up to three lines.
  char pager[16] = "";
  if (g_detail.ngroups > 1) snprintf(pager, sizeof(pager), "%d/%d", s_group + 1, g_detail.ngroups);
  int tl = g_theme.big ? 20 : 16;
  GTextAlignment talign = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  int y = c.origin.y + draw_wrapped(ctx, m->title, g_theme.small_bold,
                                    GRect(c.origin.x + g_theme.edge, c.origin.y - 3, c.size.w - 2 * g_theme.edge, tl * 3 + 2),
                                    talign, C_TEXT);

  // Readout: the probability at the selected bar.
  char digits[12];
  fmt_prob(digits, sizeof(digits), shown(p), false);
  bool res = p->flags & PF_RESOLVED;
  int rh = g_theme.big ? 44 : 32;
  draw_readout(ctx, GRect(c.origin.x, y, c.size.w, rh), NULL, false, digits, 0, "%", g_line, res);
  y += rh + 2;

  // What the bar is, and which curve (in full, on up to two lines).
  char label[28];
  bar_label(label, sizeof(label), p);
  const char *curve = g_detail.ngroups > 1 ? m->groups[s_group].label : "";
  int lh = g_theme.big ? 22 : 18;
  if (count) {
    draw_geq(ctx, GPoint(c.origin.x + 1, y + 4), 6, C_TEXT);
    draw_text(ctx, label, g_theme.small_bold, GRect(c.origin.x + 10, y - 2, c.size.w - 10, lh), GTextAlignmentLeft, C_TEXT);
  } else {
    draw_text(ctx, label, g_theme.small_bold, GRect(c.origin.x, y - 2, c.size.w, lh), GTextAlignmentLeft, C_TEXT);
  }
  // Which curve of how many, beside the bar it's on.
  if (pager[0]) draw_text(ctx, pager, g_theme.small, GRect(c.origin.x, y - 1, c.size.w - g_theme.edge, lh), GTextAlignmentRight, C_DIM);
  y += lh - 2;
  if (curve[0]) y += draw_wrapped(ctx, curve, g_theme.small, GRect(c.origin.x, y - 3, c.size.w, g_theme.lh * 2 + 2), GTextAlignmentLeft, C_DIM);

  // The point chart.
  int axis_h = g_theme.lh;
  GRect ch = GRect(c.origin.x, y + 2, c.size.w, c.origin.y + c.size.h - axis_h - y - 4);
  if (ch.size.h < 12) return;
  int pitch = ch.size.w / n;
  if (pitch > 14) pitch = 14;
  if (pitch < 3) pitch = 3;
  int visible = ch.size.w / pitch;
  if (visible > n) visible = n;
  int first = s_idx - visible / 2;
  if (first < 0) first = 0;
  if (first + visible > n) first = n - visible;
  int bw = pitch >= 9 ? pitch - 3 : (pitch >= 5 ? pitch - 2 : pitch - 1);
  int x0 = ch.origin.x + (ch.size.w - visible * pitch) / 2;
  int base = ch.origin.y + ch.size.h;

  // Even odds.
  graphics_context_set_stroke_color(ctx, C_DIM);
  for (int x = ch.origin.x; x < ch.origin.x + ch.size.w; x += 4) {
    graphics_draw_pixel(ctx, GPoint(x, ch.origin.y + ch.size.h / 2));
  }

  for (int i = 0; i < visible; i++) {
    const Point *q = &pts[first + i];
    int bx = x0 + i * pitch;
    int h = ch.size.h * shown(q) / 1000;
    if (h < 1) h = 1;
    bool sel = first + i == s_idx;
    bool qres = q->flags & PF_RESOLVED;
#if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, C_GHOST);
    graphics_fill_rect(ctx, GRect(bx, ch.origin.y, bw, ch.size.h - h), 0, GCornerNone);
    GColor col = sel ? C_LIT : (qres ? ((q->flags & PF_YES) ? C_BAR : GColorDarkGray) : C_BAND_OUT);
    if (sel && qres && (q->flags & PF_YES)) col = C_BAR;
    graphics_context_set_fill_color(ctx, col);
    graphics_fill_rect(ctx, GRect(bx, base - h, bw, h), 0, GCornerNone);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (sel) graphics_fill_rect(ctx, GRect(bx, base - h, bw, h), 0, GCornerNone);
    else fill_dither(ctx, GRect(bx, base - h, bw, h), qres && !(q->flags & PF_YES) ? 4 : 2);
#endif
    // The other line, as a tick across the bar.
    int oh = ch.size.h * other(q) / 1000;
    graphics_context_set_fill_color(ctx, sel ? C_BAND_OUT : C_TEXT);
    graphics_fill_rect(ctx, GRect(bx - (pitch >= 5 ? 1 : 0), base - oh - 1, bw + (pitch >= 5 ? 2 : 0), 1), 0, GCornerNone);
  }

  // Marker under the selected bar, and the ends of the window.
  int sx = x0 + (s_idx - first) * pitch;
  graphics_context_set_fill_color(ctx, C_TEXT);
  graphics_fill_rect(ctx, GRect(sx, base + 2, bw, 3), 0, GCornerNone);
  char l[20], r[20];
  if (count) {
    fmt_value(l, sizeof(l), pts[first].x, m, true);
    fmt_value(r, sizeof(r), pts[first + visible - 1].x, m, true);
  } else {
    fmt_date(l, sizeof(l), pts[first].x, m->cadence == CAD_YEAR ? CAD_YEAR : CAD_MONTH);
    fmt_date(r, sizeof(r), pts[first + visible - 1].x, m->cadence == CAD_YEAR ? CAD_YEAR : CAD_MONTH);
  }
  int ay = base + 3;
  draw_text(ctx, l, g_theme.small, GRect(ch.origin.x + g_theme.edge, ay, ch.size.w / 2 - g_theme.edge, g_theme.lh + 2), GTextAlignmentLeft, C_DIM);
  draw_text(ctx, r, g_theme.small, GRect(ch.origin.x + ch.size.w / 2, ay, ch.size.w / 2 - g_theme.edge, g_theme.lh + 2), GTextAlignmentRight, C_DIM);
}

static void move(int d) {
  const Point *pts;
  int n = group_points(s_group, &pts);
  int next = s_idx + d;
  if (next < 0 || next >= n) return;
  bump_detent(shown(&pts[s_idx]), shown(&pts[next]));
  s_idx = next;
  layer_mark_dirty(s_layer);
}

static void seek(int32_t x) {
  const Point *p;
  int i = point_at(s_group, x, market()->kind == KIND_COUNT, &p);
  s_idx = i < 0 ? 0 : i;
}

static void up(ClickRecognizerRef r, void *ctx) { move(-1); }
static void down(ClickRecognizerRef r, void *ctx) { move(1); }
static void select(ClickRecognizerRef r, void *ctx) {
  if (g_detail.ngroups < 2) return;
  const Point *pts;
  int n = group_points(s_group, &pts);
  int32_t x = n ? pts[s_idx < n ? s_idx : n - 1].x : 0;
  s_group = (s_group + 1) % g_detail.ngroups;
  seek(x);
  layer_mark_dirty(s_layer);
}
static void select_long(ClickRecognizerRef r, void *ctx) {
  next_line(market());
  layer_mark_dirty(s_layer);
}

static void clicks(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 90, up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 90, down);
  window_single_click_subscribe(BUTTON_ID_SELECT, select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
}

static void unload(Window *w) {
  layer_destroy(s_layer);
  s_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void detail_window_push(int market_index, int group, int32_t x) {
  s_market = market_index;
  s_group = group;
  seek(x);
  s_window = window_create();
  window_set_background_color(s_window, C_BG);
  window_set_click_config_provider(s_window, clicks);
  window_set_window_handlers(s_window, (WindowHandlers){.load = load, .unload = unload});
  window_stack_push(s_window, false);
}

void detail_window_refresh(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}
