#include "app.h"

// Level 2: the homepage card, tuned. Up and Down move the needle along the
// market's dates or thresholds; every row reads off at the needle.

static Window *s_window;
static Layer *s_layer;
static int s_market;
// The needle's value: a threshold or date of one of the curves. Up and
// Down step through every curve's, so later years' higher thresholds are
// reachable from the first year's.
static int32_t s_x;
static bool s_set;

static const Summary *market(void) { return &g_markets[s_market]; }

static bool detail_loaded(void) {
  return g_detail.ready && strcmp(g_detail.code, market()->code) == 0 && g_detail.ngroups > 0;
}

static int ladder(const Point **pts) { return group_points(ladder_group(), pts); }

static int shown_groups(void) {
  int ng = market()->ngroups < g_detail.ngroups ? market()->ngroups : g_detail.ngroups;
  return ng < 1 ? 1 : ng;
}

// The nearest threshold or date past x in direction d, over every shown
// curve; false at the end.
static bool step_from(int32_t x, int d, int32_t *out) {
  bool found = false;
  for (int g = 0; g < shown_groups(); g++) {
    const Point *pts;
    int n = group_points(g, &pts);
    for (int i = 0; i < n; i++) {
      int32_t v = pts[i].x;
      if (d > 0 ? v <= x : v >= x) continue;
      if (!found || (d > 0 ? v < *out : v > *out)) *out = v;
      found = true;
    }
  }
  return found;
}

static void init_cursor(void) {
  const Point *pts;
  int n = ladder(&pts);
  if (n == 0) return;
  const Summary *m = market();
  int32_t target = m->kind == KIND_COUNT ? m->groups[0].q[2] : m->lo;
  int best = 0;
  int32_t bd = INT32_MAX;
  for (int i = 0; i < n; i++) {
    int32_t d = pts[i].x > target ? pts[i].x - target : target - pts[i].x;
    if (d < bd) { bd = d; best = i; }
  }
  s_x = pts[best].x;
  s_set = true;
}

static int32_t cursor_x(void) { return s_x; }

// A curve's bar at x. Past the end of a curve's own bars it reads off its
// last (or, for thresholds, first) bar, and *bound says the true value is
// below ('<') or above ('>') it: P(>= x) is under P(>= top threshold), and
// P(by x) is over P(by the last date) for a separate event. A date
// market's yearly curves end with their year, so theirs stand as they are.
static const Point *row_point(int g, int32_t x, bool count, char *bound) {
  const Point *pts, *p;
  int n = group_points(g, &pts);
  *bound = 0;
  point_at(g, x, count, &p);
  if (n == 0 || !p) return p;
  if (x > pts[n - 1].x) *bound = count ? '<' : market()->kind == KIND_MULTIDATE ? '>' : 0;
  else if (count && x < pts[0].x) *bound = '>';
  return p;
}

static int value_of(const Point *p) { return line_value(p, g_line); }

static int scale_pos(int32_t v, int32_t lo, int32_t hi, int x0, int w) {
  if (hi <= lo) return x0;
  int x = x0 + (int)((int64_t)(v - lo) * w / (hi - lo));
  return x < x0 ? x0 : (x > x0 + w ? x0 + w : x);
}

static void draw_tuning(GContext *ctx, GRect c, int y) {
  // Before the bars arrive: the summary rows would mislead under a needle,
  // so the readout waits, unlit.
  int h = g_theme.big ? 40 : 30;
  draw_readout(ctx, GRect(c.origin.x, y, c.size.w, h), NULL, false, "", 4, NULL, g_line, false);
  draw_text(ctx, "Loading", g_theme.small_bold, GRect(c.origin.x, y + h + 10, c.size.w, 20), GTextAlignmentCenter, C_DIM);
}

static void update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  GRect c = content_rect(bounds);
  const Summary *m = market();
  bool count = m->kind == KIND_COUNT;
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);

  // The whole title, in what the readout, rows and dial leave (one line at
  // least).
  int y = c.origin.y;
  int tl = g_theme.big ? 20 : 16;
  int ng0 = m->ngroups > 0 ? m->ngroups : 1;
  int room = c.size.h - (g_theme.big ? 44 : 34) - ng0 * (g_theme.big ? 15 : 12) - (g_theme.lh + 6);
  if (room < tl + 2) room = tl + 2;
  if (room > tl * 4 + 2) room = tl * 4 + 2;
  y += draw_wrapped(ctx, m->title, g_theme.small_bold,
                    GRect(c.origin.x + g_theme.edge, y - 3, c.size.w - 2 * g_theme.edge, room), align, C_TEXT) + 1;

  if (!detail_loaded()) { draw_tuning(ctx, c, y + 2); return; }
  if (!s_set) init_cursor();

  const Point *lad;
  int nl = ladder(&lad);
  int32_t x = cursor_x();

  // Readout.
  char digits[16], unit[20], prefix[8] = "";
  bool settled = false;
  if (count) {
    fmt_value(digits, sizeof(digits), x, m, false);
    // A long word unit ("hurricanes") would crowd out the digits; the title says it.
    const char *word = strlen(m->word) <= 5 ? m->word : "";
    snprintf(unit, sizeof(unit), "%s%s", m->prefix, m->suffix[0] || m->prefix[0] ? m->suffix : word);
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
    if (p && (p->flags & PF_RESOLVED)) settled = true;
  }
  int rh_read = g_theme.big ? 40 : 30;
  draw_readout(ctx, GRect(c.origin.x, y, c.size.w, rh_read), prefix, count, digits, count ? 0 : 4, unit, g_line, settled);
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

  // Dates span every shown curve's bars, from the first to the last.
  int32_t lo = m->lo, hi = m->hi;
  if (!count) {
    lo = lad[0].x;
    hi = lad[nl - 1].x;
    for (int g = 0; g < shown_groups(); g++) {
      const Point *pts;
      int n = group_points(g, &pts);
      if (n && pts[0].x < lo) lo = pts[0].x;
      if (n && pts[n - 1].x > hi) hi = pts[n - 1].x;
    }
  }
  int needle = count ? scale_pos(x, lo, hi, bx, bw) : 0;

  for (int g = 0; g < ng && y + L.rh <= bottom + 2; g++) {
    char bound;
    const Point *p = row_point(g, x, count, &bound);
    int v = value_of(p);
    uint8_t flags = p ? p->flags : 0;
    GColor vcol, bcol;
    char value[12];
    row_value(value, sizeof(value), v, flags, &vcol, &bcol);
    if (bound && (flags & PF_RESOLVED)) {
      // Settled past its own bars only where the outcome carries over: no
      // above a threshold that resolved no, yes after a date that resolved yes.
      bool yes = flags & PF_YES;
      if (yes != (bound == '>')) snprintf(value, sizeof(value), "-");
    } else if (bound && v >= 0) {
      char num[10];
      fmt_prob(num, sizeof(num), v, true);
      snprintf(value, sizeof(value), "%c%s", bound, num);
    }
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
    needle = scale_pos(x, lo, hi, bx, bw);
  }
  int ly = c.origin.y + c.size.h - scale_h + 5;
  graphics_context_set_stroke_color(ctx, C_DIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(bx, ly), GPoint(bx + bw, ly));
  int last_tick = -10;
  // A tick per step: the ladder's thresholds, or every curve's dates.
  int32_t tv = count ? lad[0].x : lo;
  for (int i = 0; count ? i < nl : true; i++) {
    if (count) tv = lad[i].x;
    else if (i > 0 && !step_from(tv, 1, &tv)) break;
    int tx = scale_pos(tv, lo, hi, bx, bw);
    bool major = count ? false : (m->cadence == CAD_YEAR || date_month(tv) == 1);
    if (!major && tx - last_tick < 3) continue;
    graphics_context_set_stroke_color(ctx, major ? C_TEXT : C_DIM);
    graphics_draw_line(ctx, GPoint(tx, ly - (major ? 4 : 2)), GPoint(tx, ly));
    last_tick = tx;
  }
  char left[20], right[20];
  if (count) {
    fmt_value(left, sizeof(left), lo, m, true);
    fmt_value(right, sizeof(right), hi, m, true);
  } else {
    snprintf(left, sizeof(left), "%d", date_year(lo));
    snprintf(right, sizeof(right), "%d", date_year(hi));
  }
  // Each end's label sits centred under its end, kept on screen, so a
  // dial narrowed by the row labels still has room for both.
  int x0 = c.origin.x + g_theme.edge, x1 = c.origin.x + c.size.w - g_theme.edge;
  GRect tr = GRect(0, 0, c.size.w, g_theme.lh + 2);
  int lw = graphics_text_layout_get_content_size(left, g_theme.small, tr, GTextOverflowModeFill, GTextAlignmentLeft).w;
  int rw = graphics_text_layout_get_content_size(right, g_theme.small, tr, GTextOverflowModeFill, GTextAlignmentLeft).w;
  int lx = bx - lw / 2, rx = bx + bw - rw / 2;
  if (lx < x0) lx = x0;
  if (rx + rw > x1) rx = x1 - rw;
  if (rx < lx + lw + 4) rx = lx + lw + 4;
  draw_text(ctx, left, g_theme.small, GRect(lx, ly, lw + 2, g_theme.lh + 2), GTextAlignmentLeft, C_DIM);
  draw_text(ctx, right, g_theme.small, GRect(rx, ly, rw + 2, g_theme.lh + 2), GTextAlignmentLeft, C_DIM);

  // The needle: through the rows for thresholds, on the dial for dates.
  graphics_context_set_fill_color(ctx, C_TEXT);
  int top = count ? rows_top - 2 : ly - 7;
  graphics_fill_rect(ctx, GRect(needle - 1, top, 2, ly + 4 - top), 0, GCornerNone);
}

static void move(int d) {
  if (!detail_loaded()) return;
  int32_t next;
  if (!step_from(s_x, d, &next)) return;
  bool count = market()->kind == KIND_COUNT;
  const Point *before, *after;
  point_at(0, s_x, count, &before);
  point_at(0, next, count, &after);
  bump_detent(value_of(before), value_of(after));
  s_x = next;
  layer_mark_dirty(s_layer);
}

static void up(ClickRecognizerRef r, void *ctx) { move(-1); }
static void down(ClickRecognizerRef r, void *ctx) { move(1); }
static void select(ClickRecognizerRef r, void *ctx) {
  if (!detail_loaded()) return;
  // The first curve still open: the settled one before it is only context.
  int g = 0;
  for (int i = 0; i < shown_groups(); i++) {
    const Point *pts;
    int n = group_points(i, &pts);
    bool open = false;
    for (int k = 0; k < n && !open; k++) open = !(pts[k].flags & PF_RESOLVED);
    if (open) { g = i; break; }
  }
  detail_window_push(s_market, g, cursor_x());
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

void market_window_push(int index) {
  s_market = index;
  s_set = false;
  if (g_line == LINE_YOU && !(g_markets[index].flags & MF_MINE)) g_line = LINE_CROWD;
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
