#include "app.h"

static Window *s_window;
static Layer *s_layer;
static int s_index;

static int scale_x(int32_t v, int32_t lo, int32_t hi, GRect bar) {
  if (hi <= lo) return bar.origin.x;
  int64_t num = (int64_t)(v - lo) * bar.size.w;
  int x = bar.origin.x + (int)(num / (hi - lo));
  if (x < bar.origin.x) x = bar.origin.x;
  if (x > bar.origin.x + bar.size.w) x = bar.origin.x + bar.size.w;
  return x;
}

// One homepage count row: the 90% range, the middle half and the median.
void draw_count_band(GContext *ctx, GRect bar, const Summary *m, const SummaryGroup *g) {
  draw_track(ctx, bar);
  int x05 = scale_x(g->q[0], m->lo, m->hi, bar), x95 = scale_x(g->q[4], m->lo, m->hi, bar);
  int x25 = scale_x(g->q[1], m->lo, m->hi, bar), x75 = scale_x(g->q[3], m->lo, m->hi, bar);
  int x50 = scale_x(g->q[2], m->lo, m->hi, bar);
  GRect outer = GRect(x05, bar.origin.y, x95 - x05 + 1, bar.size.h);
  GRect inner = GRect(x25, bar.origin.y, x75 - x25 + 1, bar.size.h);
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, C_BAND_OUT);
  graphics_fill_rect(ctx, outer, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, C_BAR);
  graphics_fill_rect(ctx, inner, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(x50 - 1, bar.origin.y - 2, 2, bar.size.h + 4), 0, GCornerNone);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, outer, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  fill_dither(ctx, outer, 2);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, inner, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x50 - 1, bar.origin.y, 2, bar.size.h), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(x50 - 1, bar.origin.y - 2, 2, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x50 - 1, bar.origin.y + bar.size.h, 2, 2), 0, GCornerNone);
#endif
}

void draw_prob_bar(GContext *ctx, GRect bar, int permille, GColor color) {
  draw_track(ctx, bar);
  int w = bar.size.w * permille / 1000;
  if (permille > 0 && w < 1) w = 1;
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(bar.origin.x, bar.origin.y, w, bar.size.h), 0, GCornerNone);
}

static void draw_empty(GContext *ctx, GRect c) {
  int h = g_theme.big ? 44 : 32;
  int w = segments_width(h, "", 4);
  GRect box = GRect(c.origin.x + (c.size.w - w) / 2, c.origin.y + c.size.h / 2 - h, w, h);
#if defined(PBL_COLOR)
  draw_segments(ctx, box, "", 4, false);
#else
  draw_segments(ctx, box, "----", 4, false);
#endif
  draw_text(ctx, g_status, g_theme.small_bold, GRect(c.origin.x, box.origin.y + h + 8, c.size.w, 20),
            GTextAlignmentCenter, C_DIM);
}

static void update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  GRect c = content_rect(bounds);

  if (g_market_count == 0) {
    draw_header(ctx, c, "antistatic", NULL, false);
    draw_empty(ctx, c);
    return;
  }
  if (s_index >= g_market_count) s_index = g_market_count - 1;
  const Summary *m = &g_markets[s_index];

  char counter[16];
  snprintf(counter, sizeof(counter), "%02d/%02d", s_index + 1, g_market_total);
  draw_header(ctx, c, "antistatic", g_list_done ? counter : g_status, !g_list_done);

  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  int y = c.origin.y + g_theme.lh;
  int bottom = c.origin.y + c.size.h;
  bool resolved = m->flags & MF_RESOLVED;
  bool single = m->kind != KIND_COUNT && m->ngroups == 1 && m->groups[0].label[0] == 0;
  if (resolved || single) bottom -= g_theme.lh;
  bottom -= g_theme.edge;
  int ng = m->ngroups > 0 ? m->ngroups : 1;
  RowLayout L;
  rows_layout(&L, m, ng, c, 0, 1000);

  // The title takes three lines when the rows can spare them.
  int line = g_theme.big ? 26 : 20;
  int sub_h = m->kind != KIND_COUNT && m->sub[0] ? g_theme.lh - 1 : 0;
  int rows_min = ng * (L.stacked ? 24 : 14);
  int lines = bottom - y - sub_h - rows_min >= line * 3 + 2 ? 3 : 2;
  GRect tbox = GRect(c.origin.x, y - 2, c.size.w, line * lines);
  GSize tsz = graphics_text_layout_get_content_size(m->title, g_theme.title, tbox, GTextOverflowModeTrailingEllipsis, align);
  graphics_context_set_text_color(ctx, C_TEXT);
  graphics_draw_text(ctx, m->title, g_theme.title, tbox, GTextOverflowModeTrailingEllipsis, align, NULL);
  y += tsz.h + 2;

  if (sub_h) {
    draw_text(ctx, m->sub, g_theme.small, GRect(c.origin.x, y - 3, c.size.w, g_theme.lh + 4), align, C_DIM);
    y += sub_h;
  }

  rows_layout(&L, m, ng, c, y, bottom);
  if (single && L.rh > 22) L.rh = 22;
  for (int g = 0; g < m->ngroups && y + L.rh <= bottom + 2; g++) {
    const SummaryGroup *sg = &m->groups[g];
    char value[20];
    GColor vcol = C_LIT, bcol = C_BAR;
    if (m->kind == KIND_COUNT) fmt_value(value, sizeof(value), sg->q[2], m, true);
    else row_value(value, sizeof(value), sg->prob, sg->flags, &vcol, &bcol);
    GRect bar = row_draw(ctx, &L, c, y, sg->label, value, vcol);
    bool yes = (sg->flags & PF_RESOLVED) && (sg->flags & PF_YES);
    if (m->kind == KIND_COUNT) draw_count_band(ctx, bar, m, sg);
    else draw_prob_bar(ctx, bar, yes ? 1000 : sg->prob, bcol);
    y += L.rh;
  }

  if (single && !resolved) {
    char text[40], date[20];
    if (m->groups[0].q[2] != Q_NONE) {
      fmt_date(date, sizeof(date), m->groups[0].q[2], m->cadence);
      snprintf(text, sizeof(text), "50%% by %s", date);
    } else {
      fmt_date(date, sizeof(date), m->hi, m->cadence);
      snprintf(text, sizeof(text), "Under 50%% by %s", date);
    }
    draw_text(ctx, text, g_theme.small, GRect(c.origin.x, bottom - 1, c.size.w, 18), align, C_DIM);
  }
  if (resolved) {
    draw_text(ctx, "Resolved", g_theme.small_bold, GRect(c.origin.x, bottom - 1, c.size.w, 18), align, C_RED);
  }
}

static void step(int d) {
  if (g_market_count == 0) return;
  int next = s_index + d;
  if (next < 0 || next >= g_market_count) return;
  s_index = next;
  layer_mark_dirty(s_layer);
}

static void up(ClickRecognizerRef r, void *ctx) { step(-1); }
static void down(ClickRecognizerRef r, void *ctx) { step(1); }
static void select(ClickRecognizerRef r, void *ctx) {
  if (g_market_count > 0) market_window_push(s_index);
}
static void select_long(ClickRecognizerRef r, void *ctx) { comm_refresh(); }

static void clicks(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down);
  window_single_click_subscribe(BUTTON_ID_SELECT, select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, select_long, NULL);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  theme_init(layer_get_bounds(root));
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
}

static void unload(Window *w) { layer_destroy(s_layer); s_layer = NULL; }

void list_window_push(void) {
  s_window = window_create();
  window_set_background_color(s_window, C_BG);
  window_set_click_config_provider(s_window, clicks);
  window_set_window_handlers(s_window, (WindowHandlers){.load = load, .unload = unload});
  window_stack_push(s_window, false);
}

void list_window_refresh(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}
