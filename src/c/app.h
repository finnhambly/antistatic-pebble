#pragma once
#include <pebble.h>

// Sizes. Aplite has 24 KB for everything, so it holds fewer markets and bars.
#if defined(PBL_PLATFORM_APLITE)
#define MAX_MARKETS 8
#define MAX_POINTS 200
#define LABEL_LEN 20
#define INBOX_SIZE 1024
#else
#define MAX_MARKETS 24
#define MAX_POINTS 480
#define LABEL_LEN 28
#define INBOX_SIZE 2048
#endif
#define MAX_GROUPS 6
#define TITLE_LEN 72
#define CODE_LEN 24
#define UNIT_LEN 10
#define SUB_LEN 24

// Message types, phone -> watch.
enum {
  MSG_SUMMARY = 1,
  MSG_LIST_DONE = 2,
  MSG_DETAIL = 3,
  MSG_DETAIL_DONE = 4,
  MSG_STATUS = 5,
};
// Watch -> phone.
enum {
  MSG_HELLO = 10,
  MSG_REQUEST_DETAIL = 11,
  MSG_REFRESH = 12,
};

typedef enum { KIND_COUNT = 0, KIND_DATE = 1, KIND_MULTIDATE = 2 } MarketKind;
// Market flags.
enum { MF_RESOLVED = 1, MF_CUMULATIVE = 2 };
// Bar flags.
enum { PF_RESOLVED = 1, PF_YES = 2, PF_ENDED = 4 };
// Date cadence, from the market's x_unit.
enum { CAD_DAY = 0, CAD_WEEK = 1, CAD_MONTH = 2, CAD_QUARTER = 3, CAD_YEAR = 4 };

#define Q_NONE INT32_MIN

typedef struct {
  char label[LABEL_LEN];
  // Count: the 5th, 25th, 50th, 75th and 95th percentiles, value x 100.
  // Date: the same percentiles as days since 2000-01-01, Q_NONE past the
  // last bar.
  int32_t q[5];
  // Date: the crowd's probability (per mille) by the home date.
  uint16_t prob;
  // Date: the flags of that bar (settled, and which way).
  uint8_t flags;
} SummaryGroup;

typedef struct {
  char code[CODE_LEN];
  char title[TITLE_LEN];
  char prefix[UNIT_LEN];
  char suffix[UNIT_LEN];
  char word[UNIT_LEN];
  char sub[SUB_LEN];
  uint8_t kind, flags, ngroups, cadence;
  // Count: the axis range, value x 100. Date: lo is the home date (day).
  int32_t lo, hi;
  SummaryGroup groups[MAX_GROUPS];
} Summary;

typedef struct __attribute__((packed)) {
  int32_t x;  // count: threshold x 100; date: day since 2000-01-01
  uint16_t crowd, house;  // per mille
  uint8_t flags;
} Point;

typedef struct {
  char code[CODE_LEN];
  bool ready;
  uint8_t ngroups;
  uint16_t start[MAX_GROUPS], count[MAX_GROUPS];
  uint16_t npts;
  Point pts[MAX_POINTS];
} Detail;

// Model (main.c).
extern Summary g_markets[MAX_MARKETS];
extern int g_market_count;
extern int g_market_total;
extern bool g_list_done;
extern char g_status[48];
extern Detail g_detail;

void comm_request_detail(const char *code);
void comm_refresh(void);

// Windows.
void list_window_push(void);
void list_window_refresh(void);
void market_window_push(int index);
void market_window_refresh(void);
void detail_window_push(int market, int group, int32_t x, bool house);
void detail_window_refresh(void);

// Drawing and formatting (draw.c).
#define C_BG GColorBlack
#define C_TEXT GColorWhite
#define C_LIT PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite)
#define C_DIM PBL_IF_COLOR_ELSE(GColorCadetBlue, GColorWhite)
#define C_GHOST PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack)
#define C_BAR PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite)
#define C_BAND_OUT PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorWhite)
#define C_RED PBL_IF_COLOR_ELSE(GColorFolly, GColorWhite)

typedef struct {
  GFont small, small_bold, title;
  int pad_x, pad_y;
  int lh;    // a line of small text
  int edge;  // extra inset for the first and last lines on round screens
  bool big;
} Theme;
extern Theme g_theme;
void theme_init(GRect bounds);
GRect content_rect(GRect bounds);

void fill_dither(GContext *ctx, GRect r, int density);
void draw_track(GContext *ctx, GRect r);
void draw_segments(GContext *ctx, GRect box, const char *digits, int cells, bool right_align);
int segments_width(int h, const char *digits, int cells);
void draw_geq(GContext *ctx, GPoint at, int size, GColor color);
void draw_text(GContext *ctx, const char *s, GFont font, GRect r, GTextAlignment align, GColor color);
void draw_indicators(GContext *ctx, GRect r, bool house, bool settled, bool settled_yes);
void draw_header(GContext *ctx, GRect c, const char *left, const char *right, bool busy);
typedef struct { bool stacked; int lw, vw, rh, bx, bw; } RowLayout;
void rows_layout(RowLayout *L, const Summary *m, int ng, GRect c, int top, int bottom);
void row_value(char *buf, size_t n, int permille, uint8_t flags, GColor *col, GColor *bar);
GRect row_draw(GContext *ctx, const RowLayout *L, GRect c, int y, const char *label, const char *value, GColor vcol);
void draw_count_band(GContext *ctx, GRect bar, const Summary *m, const SummaryGroup *g);
void draw_prob_bar(GContext *ctx, GRect bar, int permille, GColor color);
void draw_readout(GContext *ctx, GRect r, const char *prefix, bool geq, const char *digits, int cells,
                  const char *unit, bool house, bool settled, bool settled_yes);

void fmt_value(char *buf, size_t n, int32_t v100, const Summary *m, bool unit);
void fmt_number(char *buf, size_t n, int32_t v100);
void fmt_prob(char *buf, size_t n, int permille, bool pct);
void fmt_date(char *buf, size_t n, int32_t day, int cadence);
void fmt_by_date(char *buf, size_t n, int32_t day, int cadence);
int date_year(int32_t day);
int date_month(int32_t day);

// Bar lookups on the loaded detail (model.c).
int group_points(int g, const Point **out);
int ladder_group(void);
int point_at(int g, int32_t x, bool count, const Point **out);
void bump_detent(int before, int after);
