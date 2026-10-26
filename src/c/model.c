#include "app.h"

int group_points(int g, const Point **out) {
  if (g < 0 || g >= g_detail.ngroups) { *out = NULL; return 0; }
  *out = &g_detail.pts[g_detail.start[g]];
  return g_detail.count[g];
}

// The group with the most bars sets the slider's steps.
int ladder_group(void) {
  int best = 0;
  for (int g = 1; g < g_detail.ngroups; g++) {
    if (g_detail.count[g] > g_detail.count[best]) best = g;
  }
  return best;
}

// The bar a group shows at slider position x. Date: the last bar on or
// before x (none before the first bar). Count: the nearest threshold.
int point_at(int g, int32_t x, bool count, const Point **out) {
  const Point *pts;
  int n = group_points(g, &pts);
  *out = NULL;
  if (n == 0) return -1;
  if (count) {
    int best = 0;
    int32_t bd = INT32_MAX;
    for (int i = 0; i < n; i++) {
      int32_t d = pts[i].x > x ? pts[i].x - x : x - pts[i].x;
      if (d < bd) { bd = d; best = i; }
    }
    *out = &pts[best];
    return best;
  }
  int found = -1;
  for (int i = 0; i < n; i++) {
    if (pts[i].x <= x) found = i; else break;
  }
  if (found >= 0) *out = &pts[found];
  return found;
}

// A bar's value on a line. Where you have no forecast, yours is the house's.
int line_value(const Point *p, int line) {
  if (!p) return -1;
  if (line == LINE_YOU && p->mine != MINE_NONE) return p->mine;
  return line == LINE_CROWD ? p->crowd : p->house;
}

const char *line_name(int line) { return line == LINE_CROWD ? "Crowd" : line == LINE_HOUSE ? "House" : "You"; }

// Crowd, house, then yours where you have a forecast on the market.
void next_line(const Summary *m) {
  g_line = (g_line + 1) % 3;
  if (g_line == LINE_YOU && !(m->flags & MF_MINE)) g_line = LINE_CROWD;
}

// A short tick in the wrist when the needle takes a line across even odds.
void bump_detent(int before, int after) {
  if (before < 0 || after < 0) return;
  if ((before < 500) != (after < 500)) {
    static const uint32_t segs[] = {18};
    VibePattern pat = {.durations = segs, .num_segments = 1};
    vibes_enqueue_custom_pattern(pat);
  }
}
