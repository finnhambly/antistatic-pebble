#include "app.h"

Summary g_markets[MAX_MARKETS];
int g_market_count = 0;
int g_market_total = 0;
bool g_list_done = false;
char g_status[48] = "Tuning";
Detail g_detail;
int g_line = LINE_CROWD;
Auth g_auth;

static char s_pending_detail[CODE_LEN];

static int32_t tuple_int(const Tuple *t) {
  if (!t) return 0;
  switch (t->length) {
    case 1: return t->value->int8;
    case 2: return t->value->int16;
    default: return t->value->int32;
  }
}

static void copy_str(char *dst, size_t n, const Tuple *t) {
  if (!t) { dst[0] = 0; return; }
  strncpy(dst, t->value->cstring, n - 1);
  dst[n - 1] = 0;
}

static int32_t rd_i32(const uint8_t *p) {
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Unit arrives as "prefix\tsuffix\tword".
static void split_unit(Summary *m, const char *s) {
  char *dst[3] = {m->prefix, m->suffix, m->word};
  int part = 0, j = 0;
  m->prefix[0] = m->suffix[0] = m->word[0] = 0;
  for (const char *p = s; *p && part < 3; p++) {
    if (*p == '\t') { dst[part][j] = 0; part++; j = 0; continue; }
    if (j < UNIT_LEN - 1) { dst[part][j++] = *p; dst[part][j] = 0; }
  }
}

// Group labels arrive newline-separated.
static void split_labels(Summary *m, const char *s) {
  int g = 0, j = 0;
  for (int i = 0; i < MAX_GROUPS; i++) m->groups[i].label[0] = 0;
  for (const char *p = s; *p && g < MAX_GROUPS; p++) {
    if (*p == '\n') { g++; j = 0; continue; }
    if (j < LABEL_LEN - 1) { m->groups[g].label[j++] = *p; m->groups[g].label[j] = 0; }
  }
}

static void on_summary(DictionaryIterator *it) {
  int index = tuple_int(dict_find(it, MESSAGE_KEY_Index));
  int total = tuple_int(dict_find(it, MESSAGE_KEY_Total));
  if (index < 0 || index >= MAX_MARKETS) return;
  if (index == 0) g_list_done = false;
  g_market_total = total < MAX_MARKETS ? total : MAX_MARKETS;

  Summary *m = &g_markets[index];
  memset(m, 0, sizeof(*m));
  copy_str(m->code, CODE_LEN, dict_find(it, MESSAGE_KEY_Code));
  copy_str(m->title, TITLE_LEN, dict_find(it, MESSAGE_KEY_Title));
  copy_str(m->sub, SUB_LEN, dict_find(it, MESSAGE_KEY_Sub));
  Tuple *unit = dict_find(it, MESSAGE_KEY_Unit);
  if (unit) split_unit(m, unit->value->cstring);
  Tuple *labels = dict_find(it, MESSAGE_KEY_Labels);
  if (labels) split_labels(m, labels->value->cstring);
  m->kind = tuple_int(dict_find(it, MESSAGE_KEY_Kind));
  m->flags = tuple_int(dict_find(it, MESSAGE_KEY_Flags));
  m->cadence = tuple_int(dict_find(it, MESSAGE_KEY_Cadence));
  m->lo = tuple_int(dict_find(it, MESSAGE_KEY_Lo));
  m->hi = tuple_int(dict_find(it, MESSAGE_KEY_Hi));

  Tuple *data = dict_find(it, MESSAGE_KEY_Data);
  int ngroups = data ? data->length / 23 : 0;
  if (ngroups > MAX_GROUPS) ngroups = MAX_GROUPS;
  m->ngroups = ngroups;
  for (int g = 0; g < ngroups; g++) {
    const uint8_t *p = data->value->data + g * 23;
    for (int k = 0; k < 5; k++) m->groups[g].q[k] = rd_i32(p + k * 4);
    m->groups[g].prob = rd_u16(p + 20);
    m->groups[g].flags = p[22];
  }
  if (index + 1 > g_market_count) g_market_count = index + 1;
  if (g_market_count > g_market_total && g_market_total > 0) g_market_count = g_market_total;
  list_window_refresh();
}

static void on_detail(DictionaryIterator *it) {
  char code[CODE_LEN];
  copy_str(code, CODE_LEN, dict_find(it, MESSAGE_KEY_Code));
  if (strcmp(code, s_pending_detail) != 0) return;
  int g = tuple_int(dict_find(it, MESSAGE_KEY_Group));
  int offset = tuple_int(dict_find(it, MESSAGE_KEY_Offset));
  int total = tuple_int(dict_find(it, MESSAGE_KEY_GroupTotal));
  Tuple *data = dict_find(it, MESSAGE_KEY_Data);
  if (g < 0 || g >= MAX_GROUPS || !data) return;

  if (strcmp(g_detail.code, code) != 0 || (g == 0 && offset == 0)) {
    memset(&g_detail, 0, sizeof(g_detail));
    strncpy(g_detail.code, code, CODE_LEN - 1);
  }
  if (offset == 0) {
    // Groups arrive in order; each takes the next run of bars.
    g_detail.start[g] = g_detail.npts;
    int room = MAX_POINTS - g_detail.npts;
    g_detail.count[g] = total < room ? total : room;
    g_detail.npts += g_detail.count[g];
    if (g + 1 > g_detail.ngroups) g_detail.ngroups = g + 1;
  }
  int n = data->length / 11;
  for (int i = 0; i < n; i++) {
    int k = offset + i;
    if (k >= g_detail.count[g]) break;
    const uint8_t *p = data->value->data + i * 11;
    Point *pt = &g_detail.pts[g_detail.start[g] + k];
    pt->x = rd_i32(p);
    pt->crowd = rd_u16(p + 4);
    pt->house = rd_u16(p + 6);
    pt->mine = rd_u16(p + 8);
    pt->flags = p[10];
  }
}

static void inbox(DictionaryIterator *it, void *ctx) {
  int type = tuple_int(dict_find(it, MESSAGE_KEY_Type));
  switch (type) {
    case MSG_SUMMARY: on_summary(it); break;
    case MSG_LIST_DONE:
      g_list_done = true;
      g_market_count = g_market_total;
      copy_str(g_status, sizeof(g_status), dict_find(it, MESSAGE_KEY_Status));
      list_window_refresh();
      break;
    case MSG_DETAIL: on_detail(it); break;
    case MSG_DETAIL_DONE: {
      char code[CODE_LEN];
      copy_str(code, CODE_LEN, dict_find(it, MESSAGE_KEY_Code));
      if (strcmp(code, g_detail.code) == 0) g_detail.ready = true;
      market_window_refresh();
      detail_window_refresh();
      break;
    }
    case MSG_AUTH: {
      int before = g_auth.state;
      Tuple *st = dict_find(it, MESSAGE_KEY_Status);
      const char *sv = st ? st->value->cstring : "";
      g_auth.state = sv[0] ? sv[0] - '0' : AUTH_OUT;
      copy_str(g_auth.code, sizeof(g_auth.code), dict_find(it, MESSAGE_KEY_Code));
      copy_str(g_auth.text, sizeof(g_auth.text), dict_find(it, MESSAGE_KEY_Title));
      if (g_auth.state != AUTH_IN && g_line == LINE_YOU) g_line = LINE_CROWD;
      account_window_refresh();
      if (before == AUTH_CODE && g_auth.state == AUTH_IN) {
        // Signed in: the list is fetched again as you.
        snprintf(g_status, sizeof(g_status), "Tuning");
        g_list_done = false;
        list_window_refresh();
      }
      break;
    }
    case MSG_STATUS:
      copy_str(g_status, sizeof(g_status), dict_find(it, MESSAGE_KEY_Status));
      list_window_refresh();
      market_window_refresh();
      break;
  }
}

static void send_type(int type, const char *code) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_Type, type);
  if (code) dict_write_cstring(out, MESSAGE_KEY_Code, code);
  app_message_outbox_send();
}

static AppTimer *s_retry;
static int s_retry_type;

static void retry_send(void *ctx) {
  s_retry = NULL;
  send_type(s_retry_type, s_retry_type == MSG_REQUEST_DETAIL ? s_pending_detail : NULL);
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  // The phone side may not be up yet on launch; try once more shortly.
  Tuple *t = dict_find(it, MESSAGE_KEY_Type);
  APP_LOG(APP_LOG_LEVEL_WARNING, "send failed: %d", (int)reason);
  s_retry_type = tuple_int(t);
  if (!s_retry) s_retry = app_timer_register(1500, retry_send, NULL);
}

void comm_request_detail(const char *code) {
  if (strcmp(g_detail.code, code) == 0 && g_detail.ready) return;
  strncpy(s_pending_detail, code, CODE_LEN - 1);
  s_pending_detail[CODE_LEN - 1] = 0;
  if (strcmp(g_detail.code, code) != 0) {
    memset(&g_detail, 0, sizeof(g_detail));
    strncpy(g_detail.code, code, CODE_LEN - 1);
  }
  send_type(MSG_REQUEST_DETAIL, code);
}

void comm_refresh(void) {
  snprintf(g_status, sizeof(g_status), "Tuning");
  g_list_done = false;
  g_detail.ready = false;
  g_detail.code[0] = 0;
  list_window_refresh();
  send_type(MSG_REFRESH, NULL);
}

void comm_sign_in(void) { send_type(MSG_SIGN_IN, NULL); }
void comm_sign_out(void) { send_type(MSG_SIGN_OUT, NULL); }

// The first few markets are kept on the watch, so the list draws at once
// on launch while the phone fetches.
#define KEEP_MARKETS 3
enum { P_LAYOUT = 1, P_COUNT = 2, P_MARKETS = 10 };

static void keep_markets(void) {
  if (!g_list_done || g_market_count == 0) return;
  int n = g_market_count < KEEP_MARKETS ? g_market_count : KEEP_MARKETS;
  persist_write_int(P_LAYOUT, sizeof(Summary));
  persist_write_int(P_COUNT, n);
  for (int i = 0; i < n; i++) {
    const uint8_t *p = (const uint8_t *)&g_markets[i];
    for (int off = 0, part = 0; off < (int)sizeof(Summary); off += PERSIST_DATA_MAX_LENGTH, part++) {
      int len = sizeof(Summary) - off < PERSIST_DATA_MAX_LENGTH ? sizeof(Summary) - off : PERSIST_DATA_MAX_LENGTH;
      persist_write_data(P_MARKETS + i * 4 + part, p + off, len);
    }
  }
}

static void restore_markets(void) {
  if (persist_read_int(P_LAYOUT) != (int)sizeof(Summary)) return;
  int n = persist_read_int(P_COUNT);
  if (n < 1 || n > KEEP_MARKETS || n > MAX_MARKETS) return;
  for (int i = 0; i < n; i++) {
    uint8_t *p = (uint8_t *)&g_markets[i];
    for (int off = 0, part = 0; off < (int)sizeof(Summary); off += PERSIST_DATA_MAX_LENGTH, part++) {
      int len = sizeof(Summary) - off < PERSIST_DATA_MAX_LENGTH ? sizeof(Summary) - off : PERSIST_DATA_MAX_LENGTH;
      if (persist_read_data(P_MARKETS + i * 4 + part, p + off, len) != len) return;
    }
  }
  g_market_count = g_market_total = n;
  snprintf(g_status, sizeof(g_status), "Tuning");
}

static void on_tap(AccelAxisType axis, int32_t direction) {
  comm_refresh();
}

#if PBL_API_EXISTS(app_glance_reload)
static void glance(AppGlanceReloadSession *session, size_t limit, void *ctx) {
  if (limit < 1 || g_market_count < 1) return;
  static char text[128];
  const Summary *m = &g_markets[0];
  char value[16] = "";
  if (m->ngroups > 0) {
    if (m->kind == KIND_COUNT) fmt_value(value, sizeof(value), m->groups[0].q[2], m, true);
    else fmt_prob(value, sizeof(value), m->groups[0].prob, true);
  }
  // "2.6% in 2030 · U.S. annual real GDP growth", "8% by end of 2027 · …"
  char when[SUB_LEN + 4];
  if (m->kind == KIND_COUNT) snprintf(when, sizeof(when), "in %s", m->groups[0].label);
  else snprintf(when, sizeof(when), "%s", m->sub);
  if (when[0] == 'B') when[0] = 'b';
  snprintf(text, sizeof(text), "%s %s · %s", value, when, m->title);
  const AppGlanceSlice slice = {
    .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text },
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
  };
  app_glance_add_slice(session, slice);
}
#endif

int main(void) {
  app_message_register_inbox_received(inbox);
  app_message_register_outbox_failed(outbox_failed);
  uint32_t in_size = app_message_inbox_size_maximum();
  if (in_size > INBOX_SIZE) in_size = INBOX_SIZE;
  app_message_open(in_size, 64);

  restore_markets();
  list_window_push();
  accel_tap_service_subscribe(on_tap);
  send_type(MSG_HELLO, NULL);

  app_event_loop();

  accel_tap_service_unsubscribe();
  keep_markets();
#if PBL_API_EXISTS(app_glance_reload)
  app_glance_reload(glance, NULL);
#endif
  return 0;
}
