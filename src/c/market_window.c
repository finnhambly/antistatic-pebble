#include "app.h"

// Level 2: the homepage card, tuned. Up and Down move the needle along the
// market's dates or thresholds; every row reads off at the needle.

static Window *s_window;
static Layer *s_layer;
static int s_market;
static int s_li = -1;  // ladder index of the needle
static bool s_house;

static const Summary *market(void) { return &g_markets[s_market]; }

static bool detail_loaded(void) {
  return g_detail.ready && strcmp(g_detail.code, market()->code) == 0 && g_detail.ngroups > 0;
}

static int ladder(const Point **pts) { return group_points(ladder_group(), pts); }

static void init_cursor(void) {
  const Point *pts;
  int n = ladder(&pts);
  if (n == 0) { s_li = -1; return; }
  const Summary *m = market();
  int32_t target = m->kind == KIND_COUNT ? m->groups[0].q[2] : m->lo;
  int best = 0;
  int32_t bd = INT32_MAX;
  for (int i = 0; i < n; i++) {
    int32_t d = pts[i].x > target ? pts[i].x - target : target - pts[i].x;
    if (d < bd) { bd = d; best = i; }
  }
  s_li = best;
}

static int32_t cursor_x(void) {
  const Point *pts;
  int n = ladder(&pts);
  if (n == 0 || s_li < 0) return 0;
  return pts[s_li < n ? s_li : n - 1].x;
}

static int value_of(const Point *p) { return p ? (s_house ? p->house : p->crowd) : -1; }

static int scale_pos(int32_t v, int32_t lo, int32_t hi, int x0, int w) {
  if (hi <= lo) return x0;
  int x = x0 + (int)((int64_t)(v - lo) * w / (hi - lo));
  return x < x0 ? x0 : (x > x0 + w ? x0 + w : x);
}

static void draw_tuning(GContext *ctx, GRect c, int y) {
  // Before the bars arrive: the summary rows would mislead under a needle,
  // so the readout waits, unlit.
  int h = g_theme.big ? 40 : 30;
  draw_readout(ctx, GRect(c.origin.x, y, c.size.w, h), NULL, false, "", 4, NULL, s_house, false, false);
  draw_text(ctx, "Tuning", g_theme.small_bold, GRect(c.origin.x, y + h + 10, c.size.w, 20), GTextAlignmentCenter, C_DIM);
}

static void update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  GRect c = content_rect(bounds);
  const Summary *m = market();
  bool count = m->kind == KIND_COUNT;
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);

  int y = c.origin.y;
  int th = g_theme.big ? 22 : 18;
  draw_text(ctx, m->title, g_theme.small_bold, GRect(c.origin.x + g_theme.edge, y - 3, c.size.w - 2 * g_theme.edge, th), align, C_TEXT);
  y += th;

  if (!detail_loaded()) { draw_tuning(ctx, c, y + 2); return; }
  if (s_li < 0) init_cursor();

  const Point *lad;
  int nl = ladder(&lad);
  int32_t x = cursor_x();

  // Readout.
  char digits[16], unit[20], prefix[8] = "";
  bool settled = false, yes = false;
  if (count) {
    fmt_value(digits, sizeof(digits), x, m, false);
    snprintf(unit, sizeof(unit), "%s%s", m->prefix, m->suffix[0] || m->prefix[0] ? m->suffix : m->word);
  } else {
    snprintf(digits, sizeof(digits), "%d", date_year(x));
    static const char *MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int mo = date_month(x);
    if (m->cadence == CAD_YEAR) unit[0] = 0;
    else if (m->cadence == CAD_QUARTER) snprintf(unit, sizeof(unit), "Q%d", (mo - 1) / 3 + 1);
    else snprintf(unit, sizeof(unit), "%s", MON[mo - 1]);
    snprintf(prefix, sizeof(prefix), "By");
  }
  for (int g = 0; g < g_detail.ngroups; g++) {
    const Point *p;
    point_at(g, x, count, &p);
    if (p && (p->flags & PF_RESOLVED)) { settled = true; if (p->flags & PF_YES) yes = true; }
  }
  int rh_read = g_theme.big ? 40 : 30;
  draw_readout(ctx, GRect(c.origin.x, y, c.size.w, rh_read), prefix, count, digits, count ? 0 : 4, unit, s_house, settled, yes);
  y += rh_read + 6;

  // Rows and the dial share one x range so the needle runs straight down.
  int scale_h = g_theme.lh + 6;
  int bottom = c.origin.y + c.size.h - scale_h;
  int ng = m->ngroups < g_detail.ngroups ? m->ngroups : g_detail.ngroups;
  if (ng < 1) ng = 1;
  RowLayout L;
  rows_layout(&L, m, ng, c, y, bottom);
  int bx = L.bx, bw = L.bw;
  int rows_top = y;

  int32_t lo = count ? m->lo : lad[0].x;
  int32_t hi = count ? m->hi : lad[nl - 1].x;
  int needle = count ? scale_pos(x, lo, hi, bx, bw) : 0;

  for (int g = 0; g < ng && y + L.rh <= bottom + 2; g++) {
    const Point *p;
    point_at(g, x, count, &p);
    int v = value_of(p);
    uint8_t flags = p ? p->flags : 0;
    GColor vcol, bcol;
    char value[12];
    row_value(value, sizeof(value), v, flags, &vcol, &bcol);
    GRect bar = row_draw(ctx, &L, c, y, m->groups[g].label, value, vcol);
    bool yes = (flags & PF_RESOLVED) && (flags & PF_YES);
    if (count) draw_count_band(ctx, bar, m, &m->groups[g]);
    else draw_prob_bar(ctx, bar, yes ? 1000 : (v < 0 ? 0 : v), bcol);
    y += L.rh;
  }

  // The dial. Thresholds share the rows' x range, so the needle runs
  // straight down through the bands; dates get the full width.
  if (!count) {
    bx = c.origin.x;
    bw = c.size.w;
    needle = scale_pos(s_li, 0, nl - 1, bx, bw);
  }
  int ly = c.origin.y + c.size.h - scale_h + 5;
  graphics_context_set_stroke_color(ctx, C_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(bx, ly), GPoint(bx + bw, ly));
  int last_tick = -10;
  for (int i = 0; i < nl; i++) {
    int tx = count ? scale_pos(lad[i].x, lo, hi, bx, bw) : scale_pos(i, 0, nl - 1, bx, bw);
    bool major = count ? false : (m->cadence == CAD_YEAR || date_month(lad[i].x) == 1);
    if (!major && tx - last_tick < 3) continue;
    graphics_context_set_stroke_color(ctx, major ? C_TEXT : C_DIM);
    graphics_draw_line(ctx, GPoint(tx, ly - (major ? 4 : 2)), GPoint(tx, ly));
    last_tick = tx;
  }
  int edge_l = c.origin.x + g_theme.edge - bx;
  if (edge_l < 0) edge_l = 0;
  char left[20], right[20];
  if (count) {
    fmt_value(left, sizeof(left), lo, m, true);
    fmt_value(right, sizeof(right), hi, m, true);
  } else {
    snprintf(left, sizeof(left), "%d", date_year(lad[0].x));
    snprintf(right, sizeof(right), "%d", date_year(lad[nl - 1].x));
  }
  draw_text(ctx, left, g_theme.small, GRect(bx - 2 + edge_l, ly, bw / 2 - edge_l, g_theme.lh + 2), GTextAlignmentLeft, C_DIM);
  draw_text(ctx, right, g_theme.small, GRect(bx + bw / 2, ly, bw / 2 + 2 - edge_l, g_theme.lh + 2), GTextAlignmentRight, C_DIM);

  // The needle: through the rows for thresholds, on the dial for dates.
  graphics_context_set_fill_color(ctx, C_RED);
  int top = count ? rows_top - 2 : ly - 7;
  graphics_fill_rect(ctx, GRect(needle - 1, top, 2, ly + 4 - top), 0, GCornerNone);
}

static void move(int d) {
  if (!detail_loaded()) return;
  const Point *pts;
  int n = ladder(&pts);
  int next = s_li + d;
  if (next < 0 || next >= n) return;
  bool count = market()->kind == KIND_COUNT;
  const Point *before, *after;
  point_at(0, pts[s_li].x, count, &before);
  point_at(0, pts[next].x, count, &after);
  bump_detent(value_of(before), value_of(after));
  s_li = next;
  layer_mark_dirty(s_layer);
}

static void up(ClickRecognizerRef r, void *ctx) { move(-1); }
static void down(ClickRecognizerRef r, void *ctx) { move(1); }
static void select(ClickRecognizerRef r, void *ctx) {
  if (detail_loaded()) detail_window_push(s_market, 0, cursor_x(), s_house);
}
static void select_long(ClickRecognizerRef r, void *ctx) {
  s_house = !s_house;
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

void market_window_push(int index) {
  s_market = index;
  s_li = -1;
  s_window = window_create();
  window_set_background_color(s_window, C_BG);
  window_set_click_config_provider(s_window, clicks);
  window_set_window_handlers(s_window, (WindowHandlers){.load = load, .unload = unload});
  window_stack_push(s_window, false);
  comm_request_detail(g_markets[index].code);
}

void market_window_refresh(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}
