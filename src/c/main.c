// kWeather: now, rain chart, temperature curve and the week, all drawn by hand.
// Up / Down change page, Select refreshes.
#include <pebble.h>
// #define KW_DEMO 1   // fake forecast, for emulator screenshots

#define NH 24
#define ND 7
#define NPAGES 3
enum { K_SUN, K_PART, K_CLOUD, K_FOG, K_DRIZ, K_RAIN, K_SNOW, K_STORM };

typedef struct {
  char city[24];
  int16_t t, feels, hi, lo, wind, hum, code, isday, hour0, wday0;
  int32_t when;
  int8_t ht[NH]; uint8_t hp[NH], hmm[NH], hc[NH];
  uint8_t dc[ND], dp[ND]; int8_t dhi[ND], dlo[ND];
} Wx;

static Window *s_win;
static Layer *s_canvas;
static Wx w;
static bool s_have;
static int s_page;
static char s_msg[32] = "Finding you...";
static const char *DAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// ------------------------------------------------------------------ weather codes
static int kind(int c) {
  if (c == 0) return K_SUN;
  if (c <= 2) return K_PART;
  if (c == 3) return K_CLOUD;
  if (c <= 48) return K_FOG;
  if (c <= 57) return K_DRIZ;
  if (c <= 67 || (c >= 80 && c <= 82)) return K_RAIN;
  if (c <= 77 || c == 85 || c == 86) return K_SNOW;
  return K_STORM;
}
static const char *cname(int c) {
  switch (c) {
    case 0: return "Clear"; case 1: return "Mostly clear"; case 2: return "Partly cloudy"; case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: case 56: case 57: return "Drizzle";
    case 61: case 80: return "Light rain"; case 63: case 81: return "Rain"; case 65: case 82: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 85: return "Light snow"; case 73: return "Snow"; case 75: case 86: return "Heavy snow"; case 77: return "Snow grains";
    default: return "Thunderstorm";
  }
}
static GColor sky(int code, bool day) {
#if defined(PBL_COLOR)
  int k = kind(code);
  if (!day) return k <= K_PART ? GColorOxfordBlue : k == K_STORM ? GColorImperialPurple : GColorDarkGray;
  switch (k) {
    case K_SUN: return GColorVividCerulean;
    case K_PART: return GColorPictonBlue;
    case K_CLOUD: case K_FOG: return GColorDarkGray;
    case K_SNOW: return GColorCadetBlue;
    case K_STORM: return GColorImperialPurple;
    default: return GColorCobaltBlue;
  }
#else
  (void)code; (void)day; return GColorBlack;
#endif
}
static GColor temp_color(int t) {
#if defined(PBL_COLOR)
  if (t <= 0) return GColorCeleste;
  if (t <= 8) return GColorPictonBlue;
  if (t <= 15) return GColorMediumAquamarine;
  if (t <= 22) return GColorIcterine;
  if (t <= 28) return GColorChromeYellow;
  return GColorOrange;
#else
  (void)t; return GColorWhite;
#endif
}

// ------------------------------------------------------------------ icons, drawn with shapes
#define SC(v, p) ((v) * (p) / 100)
static void cloud(GContext *ctx, GPoint c, int r, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(c.x - SC(r, 36), c.y + SC(r, 8)), SC(r, 34));
  graphics_fill_circle(ctx, GPoint(c.x + SC(r, 2), c.y - SC(r, 14)), SC(r, 46));
  graphics_fill_circle(ctx, GPoint(c.x + SC(r, 42), c.y + SC(r, 12)), SC(r, 30));
  graphics_fill_rect(ctx, GRect(c.x - SC(r, 36), c.y + SC(r, 8), SC(r, 78), SC(r, 34) + 1), 0, GCornerNone);
}
static void sun(GContext *ctx, GPoint c, int r) {
  GColor col = PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, r >= 24 ? 3 : 2);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8, s = sin_lookup(a), co = cos_lookup(a);
    GPoint p1 = GPoint(c.x + s * SC(r, 60) / TRIG_MAX_RATIO, c.y - co * SC(r, 60) / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(c.x + s * SC(r, 86) / TRIG_MAX_RATIO, c.y - co * SC(r, 86) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, c, SC(r, 42));
}
static void moon(GContext *ctx, GPoint c, int r, GColor bg) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite));
  graphics_fill_circle(ctx, c, SC(r, 46));
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, GPoint(c.x + SC(r, 24), c.y - SC(r, 18)), SC(r, 38));
}
static GPoint s_bolt[7];
static void bolt(GContext *ctx, GPoint c, int r) {
  static const int8_t P[7][2] = { {8, 20}, {-14, 60}, {2, 60}, {-8, 96}, {24, 46}, {6, 46}, {18, 20} };
  for (int i = 0; i < 7; i++) s_bolt[i] = GPoint(c.x + SC(r, P[i][0]), c.y + SC(r, P[i][1]));
  GPathInfo info = { 7, s_bolt };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack));
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}
static void icon(GContext *ctx, int code, GPoint c, int r, bool day, GColor bg) {
  int k = kind(code);
  GColor white = GColorWhite, grey = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
  GPoint up = GPoint(c.x, c.y - SC(r, 22));
  int sw = r >= 24 ? 3 : 2;
  switch (k) {
    case K_SUN: if (day) sun(ctx, c, r); else moon(ctx, c, r, bg); break;
    case K_PART: {
      GPoint s = GPoint(c.x - SC(r, 26), c.y - SC(r, 26));
      if (day) sun(ctx, s, SC(r, 80)); else moon(ctx, s, SC(r, 90), bg);
      cloud(ctx, GPoint(c.x + SC(r, 10), c.y + SC(r, 16)), SC(r, 84), white);
    } break;
    case K_CLOUD:
      cloud(ctx, GPoint(c.x + SC(r, 22), c.y - SC(r, 22)), SC(r, 70), grey);
      cloud(ctx, GPoint(c.x - SC(r, 6), c.y + SC(r, 6)), r, white);
      break;
    case K_FOG:
      cloud(ctx, up, SC(r, 92), grey);
      graphics_context_set_fill_color(ctx, white);
      for (int i = 0; i < 3; i++)
        graphics_fill_rect(ctx, GRect(c.x - SC(r, 50) + (i == 1 ? SC(r, 14) : 0), c.y + SC(r, 32) + i * SC(r, 18), SC(r, 86), sw), 1, GCornersAll);
      break;
    case K_SNOW:
      cloud(ctx, up, r, white);
      graphics_context_set_fill_color(ctx, white);
      for (int i = 0; i < 3; i++) {
        graphics_fill_circle(ctx, GPoint(c.x - SC(r, 34) + i * SC(r, 34), c.y + SC(r, 42)), sw);
        if (i < 2) graphics_fill_circle(ctx, GPoint(c.x - SC(r, 17) + i * SC(r, 34), c.y + SC(r, 70)), sw);
      }
      break;
    case K_STORM:
      cloud(ctx, up, r, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
      bolt(ctx, c, r);
      break;
    default: {                                      // rain and drizzle
      cloud(ctx, up, r, white);
      graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
      graphics_context_set_stroke_width(ctx, sw);
      int n = k == K_RAIN ? 3 : 2, len = k == K_RAIN ? 30 : 16;
      for (int i = 0; i < n; i++) {
        int x = c.x + (n == 3 ? (i - 1) * SC(r, 32) : (i ? SC(r, 18) : -SC(r, 18)));
        graphics_draw_line(ctx, GPoint(x, c.y + SC(r, 36)), GPoint(x - SC(r, 10), c.y + SC(r, 36 + len)));
      }
    }
  }
}

// ------------------------------------------------------------------ text helpers
static void text(GContext *ctx, const char *s, const char *font, GRect r, GTextAlignment al, GColor col) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, fonts_get_system_font(font), r, GTextOverflowModeTrailingEllipsis, al, NULL);
}
// big temperature centred on cx: LECO digits, a drawn minus, a drawn degree ring
static void big_temp(GContext *ctx, int t, int cx, int y, GColor col) {
  char b[6]; snprintf(b, sizeof(b), "%d", t < 0 ? -t : t);
  GFont f = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GSize sz = graphics_text_layout_get_content_size(b, f, GRect(0, 0, 200, 50), GTextOverflowModeFill, GTextAlignmentLeft);
  int minus = t < 0 ? 14 : 0, total = minus + sz.w + 12, x = cx - total / 2;
  graphics_context_set_fill_color(ctx, col);
  if (minus) graphics_fill_rect(ctx, GRect(x, y + 24, 10, 4), 0, GCornerNone);
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, b, f, GRect(x + minus, y, sz.w + 4, 50), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, GPoint(x + minus + sz.w + 7, y + 12), 4);
}
static void chip(GContext *ctx, const char *s, const char *font, int cx, int y, int h, GColor bg, GColor fg) {
  GFont f = fonts_get_system_font(font);
  GSize sz = graphics_text_layout_get_content_size(s, f, GRect(0, 0, 220, h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int wdt = sz.w + 20;
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(cx - wdt / 2, y, wdt, h), h / 2, GCornersAll);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, s, f, GRect(cx - wdt / 2, y + (h - sz.h) / 2 - 3, wdt, h), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static bool is_day_hour(int i) { int h = (w.hour0 + i) % 24; return h >= 7 && h < 19; }

// headline about rain: when it starts, or how long it lasts
static void rain_summary(char *b, int n, char *sub, int sn) {
  int first = -1, maxp = 0;
  for (int i = 0; i < NH; i++) {
    if (w.hp[i] > maxp) maxp = w.hp[i];
    if (first < 0 && w.hp[i] >= 40 && w.hmm[i] >= 2) first = i;
  }
  if (first < 0) { snprintf(b, n, "Dry next 24 h"); snprintf(sub, sn, "Rain chance %d%% at most", maxp); return; }
  int mm = w.hmm[first];
  if (first == 0) {
    int end = 0; while (end < NH && w.hmm[end] >= 2) end++;
    if (end >= NH) snprintf(b, n, "Rain all day"); else snprintf(b, n, "Rain until %d:00", (w.hour0 + end) % 24);
    snprintf(sub, sn, "%d.%d mm this hour", mm / 10, mm % 10);
  } else {
    snprintf(b, n, "Rain at %d:00", (w.hour0 + first) % 24);
    snprintf(sub, sn, "%d%% chance, %d.%d mm", w.hp[first], mm / 10, mm % 10);
  }
}

// ------------------------------------------------------------------ pages
static void page_now(GContext *ctx, GRect b, bool big) {
  int W = b.size.w, H = b.size.h;
  GColor bg = sky(w.code, w.isday);
  graphics_context_set_fill_color(ctx, bg); graphics_fill_rect(ctx, b, 0, GCornerNone);
  text(ctx, w.city[0] ? w.city : "Here", big ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD,
       GRect(20, PBL_IF_ROUND_ELSE(14, 0), W - 40, 28), GTextAlignmentCenter, GColorWhite);
  int iy = H * PBL_IF_ROUND_ELSE(37, 35) / 100, r = W * 19 / 100;
  icon(ctx, w.code, GPoint(W * PBL_IF_ROUND_ELSE(33, 29) / 100, iy), r, w.isday, bg);
  big_temp(ctx, w.t, W * PBL_IF_ROUND_ELSE(67, 70) / 100, iy - 27, GColorWhite);
  int y = H * PBL_IF_ROUND_ELSE(55, 57) / 100;
  text(ctx, cname(w.code), big ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD, GRect(4, y, W - 8, 28), GTextAlignmentCenter, GColorWhite);
  char l[40]; snprintf(l, sizeof(l), "%d° / %d°   Feels %d°", w.hi, w.lo, w.feels);
  text(ctx, l, big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14, GRect(4, y + (big ? 25 : 19), W - 8, 22), GTextAlignmentCenter,
       PBL_IF_COLOR_ELSE(GColorWhite, GColorWhite));
  char rb[32], rs[32]; rain_summary(rb, sizeof(rb), rs, sizeof(rs));
  int ch = big ? 28 : 22;
  chip(ctx, rb, big ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD, W / 2, H - ch - PBL_IF_ROUND_ELSE(16, 5), ch,
       PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite), PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
}

static void header(GContext *ctx, GRect b, bool big, const char *t1, const char *t2, GColor c2) {
  int W = b.size.w, y = PBL_IF_ROUND_ELSE(16, 2);
  text(ctx, t1, big ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD, GRect(6, y, W - 12, 32), GTextAlignmentCenter, GColorWhite);
  text(ctx, t2, big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14, GRect(6, y + (big ? 30 : 25), W - 12, 22), GTextAlignmentCenter, c2);
}


static GPoint s_curve[NH + 2];


// rain bars (bottom band) and temperature curve (above) on one chart, next 24 h
static void page_chart(GContext *ctx, GRect b, bool big) {
  int W = b.size.w, H = b.size.h;
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack)); graphics_fill_rect(ctx, b, 0, GCornerNone);
  char rb[32], rs[32]; rain_summary(rb, sizeof(rb), rs, sizeof(rs));
  header(ctx, b, big, rb, rs, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
  int mx = PBL_IF_ROUND_ELSE(30, 10), x0 = mx, x1 = W - mx - PBL_IF_ROUND_ELSE(0, 6);
  int ir = big ? 13 : 9;
  int iy = H * PBL_IF_ROUND_ELSE(38, 35) / 100;
  int yb = H - PBL_IF_ROUND_ELSE(38, 20);              // baseline
  int rainH = (yb - iy) * 38 / 100;                    // rain band height
  int ty0 = iy + ir + 16, ty1 = yb - rainH - 4;        // temperature band
  // rain band: faint guide at 100%, one bar per hour
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorDarkGray));
  for (int x = x0; x < x1; x += 4) graphics_draw_pixel(ctx, GPoint(x, yb - rainH));
  graphics_draw_line(ctx, GPoint(x0, yb), GPoint(x1, yb));
  int step = (x1 - x0) * 100 / NH, bw = step / 100 - 1; if (bw < 2) bw = 2;
  for (int i = 0; i < NH; i++) {
    int p = w.hp[i], mm = w.hmm[i], h = rainH * p / 100; if (p > 0 && h < 2) h = 2;
    if (!h) continue;
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(mm == 0 ? GColorLiberty : mm < 10 ? GColorPictonBlue : mm < 40 ? GColorVividCerulean : GColorBlueMoon, GColorWhite));
    graphics_fill_rect(ctx, GRect(x0 + i * step / 100, yb - h, bw, h), 1, GCornersTop);
  }
  // temperature curve
  int mn = 999, mxv = -999, imn = 0, imx = 0;
  for (int i = 0; i < NH; i++) { if (w.ht[i] < mn) { mn = w.ht[i]; imn = i; } if (w.ht[i] > mxv) { mxv = w.ht[i]; imx = i; } }
  int range = mxv - mn; if (range < 4) range = 4;
  for (int i = 0; i < NH; i++)
    s_curve[i] = GPoint(x0 + i * step / 100 + bw / 2, ty1 - (w.ht[i] - mn) * (ty1 - ty0) / range);
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i + 1 < NH; i++) graphics_draw_line(ctx, s_curve[i], s_curve[i + 1]);
  char l[8];
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, s_curve[imx], 3);
  snprintf(l, sizeof(l), "%d°", mxv);
  text(ctx, l, FONT_KEY_GOTHIC_14_BOLD, GRect(s_curve[imx].x - 20, s_curve[imx].y - 19, 40, 18), GTextAlignmentCenter, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  if (imn != imx) {
    graphics_fill_circle(ctx, s_curve[imn], 3);
    snprintf(l, sizeof(l), "%d°", mn);
    text(ctx, l, FONT_KEY_GOTHIC_14_BOLD, GRect(s_curve[imn].x - 20, s_curve[imn].y - 19, 40, 18), GTextAlignmentCenter, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
  }
  // weather icons along the top, hours along the bottom
  for (int i = 1; i < NH; i += 6) {
    int x = x0 + i * step / 100 + bw / 2;
    icon(ctx, w.hc[i], GPoint(x, iy), ir, is_day_hour(i), PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack));
    char h[6]; snprintf(h, sizeof(h), "%d:00", (w.hour0 + i) % 24);
    text(ctx, h, FONT_KEY_GOTHIC_14, GRect(x - 20, yb + 1, 40, 18), GTextAlignmentCenter, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
  }
}

static void page_week(GContext *ctx, GRect b, bool big) {
  int W = b.size.w, H = b.size.h;
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack)); graphics_fill_rect(ctx, b, 0, GCornerNone);
  int hy = PBL_IF_ROUND_ELSE(14, 0);
  text(ctx, "This week", big ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD, GRect(4, hy, W - 8, 28), GTextAlignmentCenter, GColorWhite);
  int n = PBL_IF_ROUND_ELSE(5, ND);
  int mx = PBL_IF_ROUND_ELSE(22, 4), cw = W - 2 * mx - PBL_IF_ROUND_ELSE(0, 6);
  int top = hy + (big ? 30 : 23), rh = (H - top - PBL_IF_ROUND_ELSE(24, 2)) / n;
  int lo = 99, hi = -99;
  for (int i = 0; i < n; i++) { if (w.dlo[i] < lo) lo = w.dlo[i]; if (w.dhi[i] > hi) hi = w.dhi[i]; }
  int span = hi - lo; if (span < 1) span = 1;
  const char *fd = big ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD, *ft = big ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14;
  // columns (fractions of the content width)
  int cDay = SC(cw, big ? 24 : 22), cIcon = SC(cw, big ? 29 : 30), cP0 = SC(cw, 38), cLo = SC(cw, big ? 64 : 56),
      cB0 = SC(cw, big ? 66 : 58), cB1 = SC(cw, big ? 85 : 81), cHi = SC(cw, 84);
  for (int i = 0; i < n; i++) {
    int y = top + i * rh, cy = y + rh / 2;
    if (i % 2 == 0) {
      graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
      PBL_IF_COLOR_ELSE(graphics_fill_rect(ctx, GRect(mx - 2, y, cw + 4, rh), 4, GCornersAll), (void)0);
    }
    const char *dn = i == 0 && big ? "Today" : DAYS[(w.wday0 + i) % 7];
    text(ctx, dn, fd, GRect(mx + 2, cy - (big ? 13 : 10), cDay, 22), GTextAlignmentLeft, i == 0 ? PBL_IF_COLOR_ELSE(GColorIcterine, GColorWhite) : GColorWhite);
    icon(ctx, w.dc[i], GPoint(mx + cIcon, cy), big ? 12 : 9, true, PBL_IF_COLOR_ELSE(i % 2 ? GColorBlack : GColorDarkGray, GColorBlack));
    char s[8];
    if (big && w.dp[i] >= 20) {
      snprintf(s, sizeof(s), "%d%%", w.dp[i]);
      text(ctx, s, FONT_KEY_GOTHIC_14_BOLD, GRect(mx + cP0, cy - 9, SC(cw, 16), 18), GTextAlignmentLeft, PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite));
    }
    snprintf(s, sizeof(s), "%d°", w.dlo[i]);
    text(ctx, s, ft, GRect(mx + cLo - 30, cy - (big ? 12 : 10), 30, 20), GTextAlignmentRight, PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite));
    // range bar: the whole week's span, this day's part coloured by warmth
    int bx0 = mx + cB0 + 2, bx1 = mx + cB1;
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlack, GColorDarkGray));
    graphics_fill_rect(ctx, GRect(bx0, cy - 2, bx1 - bx0, 5), 2, GCornersAll);
    int a = bx0 + (w.dlo[i] - lo) * (bx1 - bx0) / span, z = bx0 + (w.dhi[i] - lo) * (bx1 - bx0) / span;
    if (z - a < 5) z = a + 5;
    graphics_context_set_fill_color(ctx, temp_color(w.dhi[i]));
    graphics_fill_rect(ctx, GRect(a, cy - 2, z - a, 5), 2, GCornersAll);
    snprintf(s, sizeof(s), "%d°", w.dhi[i]);
    text(ctx, s, fd, GRect(mx + cHi, cy - (big ? 13 : 10), cw - cHi - 1, 22), GTextAlignmentRight, GColorWhite);
  }
}

static void dots(GContext *ctx, GRect b) {
  int W = b.size.w, H = b.size.h;
  for (int i = 0; i < NPAGES; i++) {
#if defined(PBL_ROUND)
    GPoint c = GPoint(W / 2 - 8 + i * 8, H - 8);
#else
    GPoint c = GPoint(W - 4, H / 2 - 8 + i * 8);
#endif
    graphics_context_set_fill_color(ctx, i == s_page ? GColorWhite : PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
    graphics_fill_circle(ctx, c, i == s_page ? 3 : 2);
  }
}

static void canvas_draw(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  bool big = b.size.w >= 200;
  graphics_context_set_antialiased(ctx, true);
  if (!s_have) {
    graphics_context_set_fill_color(ctx, sky(2, true)); graphics_fill_rect(ctx, b, 0, GCornerNone);
    icon(ctx, 2, GPoint(b.size.w / 2, b.size.h * 38 / 100), b.size.w / 5, true, sky(2, true));
    text(ctx, "kWeather", FONT_KEY_GOTHIC_28_BOLD, GRect(0, b.size.h * 58 / 100, b.size.w, 32), GTextAlignmentCenter, GColorWhite);
    text(ctx, s_msg, FONT_KEY_GOTHIC_18, GRect(10, b.size.h * 58 / 100 + 30, b.size.w - 20, 44), GTextAlignmentCenter, GColorWhite);
    return;
  }
  switch (s_page) {
    case 0: page_now(ctx, b, big); break;
    case 1: page_chart(ctx, b, big); break;
    default: page_week(ctx, b, big);
  }
  dots(ctx, b);
}

// ------------------------------------------------------------------ phone
static void request(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) == APP_MSG_OK) { dict_write_uint8(it, MESSAGE_KEY_CMD, 1); app_message_outbox_send(); }
}
static void inbox(DictionaryIterator *it, void *ctx) {
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_ERR))) { strncpy(s_msg, t->value->cstring, sizeof(s_msg) - 1); layer_mark_dirty(s_canvas); return; }
  if (!dict_find(it, MESSAGE_KEY_HT)) return;
#define I(k, f) if ((t = dict_find(it, MESSAGE_KEY_##k))) w.f = t->value->int32;
  I(T, t) I(FEELS, feels) I(HI, hi) I(LO, lo) I(WIND, wind) I(HUM, hum) I(CODE, code) I(ISDAY, isday) I(HOUR0, hour0) I(WDAY0, wday0)
  if ((t = dict_find(it, MESSAGE_KEY_CITY))) { strncpy(w.city, t->value->cstring, sizeof(w.city) - 1); w.city[sizeof(w.city) - 1] = 0; }
#define A(k, f, n, off) if ((t = dict_find(it, MESSAGE_KEY_##k))) for (int i = 0; i < n && i < t->length; i++) w.f[i] = (int)t->value->data[i] - off;
  A(HT, ht, NH, 100) A(HP, hp, NH, 0) A(HMM, hmm, NH, 0) A(HC, hc, NH, 0)
  A(DC, dc, ND, 0) A(DP, dp, ND, 0) A(DHI, dhi, ND, 100) A(DLO, dlo, ND, 100)
  w.when = time(NULL);
  persist_write_data(1, &w, sizeof(w));
  s_have = true;
  layer_mark_dirty(s_canvas);
}

static void tick(struct tm *tm, TimeUnits u) {
  if (time(NULL) - w.when > 30 * 60) request();
}

// ------------------------------------------------------------------ buttons
static void go(int d) { s_page = (s_page + d + NPAGES) % NPAGES; layer_mark_dirty(s_canvas); }
static void up_click(ClickRecognizerRef r, void *c) { go(-1); }
static void down_click(ClickRecognizerRef r, void *c) { go(1); }
static void select_click(ClickRecognizerRef r, void *c) { vibes_short_pulse(); request(); }
static void clicks(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

#ifdef KW_DEMO
static void demo(void) {
  strcpy(w.city, "London");
  w.t = 12; w.feels = 10; w.hi = 15; w.lo = 7; w.wind = 14; w.hum = 72; w.code = 2; w.isday = 1; w.hour0 = 13; w.wday0 = 6;
  static const int8_t T[NH] = { 12, 13, 14, 15, 14, 13, 12, 11, 10, 10, 9, 9, 8, 8, 8, 7, 7, 7, 8, 9, 10, 11, 12, 13 };
  static const uint8_t P[NH] = { 5, 10, 20, 55, 80, 90, 70, 40, 20, 10, 5, 0, 0, 0, 5, 10, 10, 5, 0, 0, 10, 20, 30, 20 };
  static const uint8_t M[NH] = { 0, 0, 1, 6, 21, 45, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1 };
  static const uint8_t C[NH] = { 2, 2, 3, 61, 63, 65, 61, 3, 3, 2, 1, 0, 0, 0, 0, 0, 1, 2, 2, 3, 3, 51, 3, 2 };
  for (int i = 0; i < NH; i++) { w.ht[i] = T[i]; w.hp[i] = P[i]; w.hmm[i] = M[i]; w.hc[i] = C[i]; }
  static const uint8_t DC[ND] = { 61, 0, 2, 95, 71, 3, 45 }, DP[ND] = { 90, 0, 10, 70, 60, 20, 30 };
  static const int8_t DH[ND] = { 15, 17, 19, 16, 3, 9, 11 }, DL[ND] = { 7, 6, 9, 10, -2, 1, 4 };
  for (int i = 0; i < ND; i++) { w.dc[i] = DC[i]; w.dp[i] = DP[i]; w.dhi[i] = DH[i]; w.dlo[i] = DL[i]; }
  w.when = time(NULL); s_have = true;
}
#endif

static void win_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_draw);
  layer_add_child(root, s_canvas);
}

int main(void) {
  if (persist_exists(1) && persist_get_size(1) == (int)sizeof(w)) { persist_read_data(1, &w, sizeof(w)); s_have = true; }
#ifdef KW_DEMO
  demo();
#endif
  s_win = window_create();
  window_set_click_config_provider(s_win, clicks);
  window_set_window_handlers(s_win, (WindowHandlers){ .load = win_load });
  window_stack_push(s_win, true);
  app_message_register_inbox_received(inbox);
  app_message_open(512, 64);
  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  app_event_loop();
  window_destroy(s_win);
}
