#include "app.h"

// Sign-in: the watch shows a code to type at antistatic.exchange/link on any
// signed-in browser; the phone waits for the approval and keeps the tokens.
// Once in, your forecast becomes a line and your private markets show.

static Window *s_window;
static Layer *s_layer;
static int s_seen = -1;
static AppTimer *s_close;

static bool can_start(void) {
  return g_auth.state == AUTH_OUT || g_auth.state == AUTH_EXPIRED || g_auth.state == AUTH_DENIED ||
         g_auth.state == AUTH_ERROR;
}

static void lines(GContext *ctx, GRect c, int y, const char *big, const char *small, GColor big_col) {
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  int line = g_theme.big ? 26 : 20;
  graphics_context_set_text_color(ctx, big_col);
  graphics_draw_text(ctx, big, g_theme.title, GRect(c.origin.x, y - 2, c.size.w, line * 2), GTextOverflowModeTrailingEllipsis,
                     align, NULL);
  GSize sz = graphics_text_layout_get_content_size(big, g_theme.title, GRect(0, 0, c.size.w, line * 2),
                                                   GTextOverflowModeTrailingEllipsis, align);
  if (small) draw_text(ctx, small, g_theme.small, GRect(c.origin.x, y + sz.h + 2, c.size.w, g_theme.lh * 2 + 4), align, C_DIM);
}

static void update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  GRect c = content_rect(bounds);
  draw_header(ctx, c, "antistatic", "Account", false);
  int y = c.origin.y + g_theme.lh + 6;
  int foot = c.origin.y + c.size.h - g_theme.lh - g_theme.edge;
  GTextAlignment align = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);

  switch (g_auth.state) {
    case AUTH_CODE: {
      // "antistatic.exchange" over "/link", from the link the phone sent.
      const char *link = g_auth.text;
      if (strncmp(link, "https://", 8) == 0) link += 8;
      else if (strncmp(link, "http://", 7) == 0) link += 7;
      const char *slash = strchr(link, '/');
      char host[32];
      int hn = slash ? slash - link : (int)strlen(link);
      if (hn > (int)sizeof(host) - 1) hn = sizeof(host) - 1;
      memcpy(host, link, hn);
      host[hn] = 0;
      int bh = g_theme.big ? 22 : 18;
      draw_text(ctx, host, g_theme.small_bold, GRect(c.origin.x, y - 3, c.size.w, bh + 2), align, C_TEXT);
      y += bh - 2;
      draw_text(ctx, slash ? slash : "", g_theme.small_bold, GRect(c.origin.x, y - 3, c.size.w, bh + 2), align, C_TEXT);
      y += bh + 4;

      // The code, four digits a line.
      int gap = 6;
      int h = (foot - y - gap - 4) / 2;
      if (h > (g_theme.big ? 44 : 34)) h = g_theme.big ? 44 : 34;
      char half[2][5] = {"", ""};
      memcpy(half[0], g_auth.code, 4);
      memcpy(half[1], g_auth.code + 4, 4);
      int w = segments_width(h, "8888", 4);
      int x = PBL_IF_ROUND_ELSE(c.origin.x + (c.size.w - w) / 2, c.origin.x);
      for (int i = 0; i < 2; i++) draw_segments(ctx, GRect(x, y + i * (h + gap), w, h), half[i], 4, false);
      draw_text(ctx, "Waiting", g_theme.small, GRect(c.origin.x, foot - 3, c.size.w, g_theme.lh + 4), align, C_DIM);
      break;
    }
    case AUTH_IN:
      lines(ctx, c, y, g_auth.text[0] ? g_auth.text : "Signed in", "Your forecasts and private markets", C_TEXT);
      draw_text(ctx, "Hold to sign out", g_theme.small, GRect(c.origin.x, foot - 3, c.size.w, g_theme.lh + 4), align, C_DIM);
      break;
    case AUTH_STARTING:
      lines(ctx, c, y, "Getting a code", NULL, C_DIM);
      break;
    default: {
      const char *what = g_auth.state == AUTH_EXPIRED ? "Code expired"
                         : g_auth.state == AUTH_DENIED ? "Declined"
                         : g_auth.state == AUTH_ERROR  ? "No signal"
                                                       : "Not signed in";
      lines(ctx, c, y, what, NULL, g_auth.state == AUTH_OUT ? C_TEXT : C_RED);
      draw_text(ctx, g_auth.state == AUTH_OUT ? "Select to sign in" : "Select to try again", g_theme.small,
                GRect(c.origin.x, foot - 3, c.size.w, g_theme.lh + 4), align, C_DIM);
    }
  }
}

static void close_now(void *ctx) {
  s_close = NULL;
  if (s_window) window_stack_remove(s_window, false);
}

static void select(ClickRecognizerRef r, void *ctx) {
  if (can_start()) comm_sign_in();
}

static void select_long(ClickRecognizerRef r, void *ctx) {
  if (g_auth.state == AUTH_IN) comm_sign_out();
}

static void clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, select_long, NULL);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
}

static void unload(Window *w) {
  if (s_close) { app_timer_cancel(s_close); s_close = NULL; }
  layer_destroy(s_layer);
  s_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void account_window_push(void) {
  if (s_window) return;
  s_seen = g_auth.state;
  s_window = window_create();
  window_set_background_color(s_window, C_BG);
  window_set_click_config_provider(s_window, clicks);
  window_set_window_handlers(s_window, (WindowHandlers){.load = load, .unload = unload});
  window_stack_push(s_window, false);
  // Signed out: straight to a code.
  if (g_auth.state == AUTH_OUT) comm_sign_in();
}

void account_window_refresh(void) {
  if (!s_layer) return;
  if (s_seen != AUTH_IN && g_auth.state == AUTH_IN) {
    // Approved: two taps, then back to the markets.
    vibes_double_pulse();
    if (!s_close) s_close = app_timer_register(1500, close_now, NULL);
  }
  s_seen = g_auth.state;
  layer_mark_dirty(s_layer);
}
