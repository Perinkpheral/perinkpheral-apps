/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Sunrise, sunset and the length of the day where the user is, and the
 * moon as it is now: four Perinkpheral OS cells. Nothing is asked of the
 * network - all of it is worked out here, from the place and the clock.
 *
 * The sun is NOAA's sunrise equation: the mean anomaly, the equation of
 * the centre, the ecliptic longitude, the transit and the hour angle at
 * which the upper limb meets the horizon, refraction included (-0.833
 * degrees). The moon is Meeus's low-precision phase angle (chapter 48):
 * the elongation and six periodic terms, about a per cent off at worst.
 *
 * There is no floating point here: angles are millionths of a degree,
 * time is millionths of a day, a sine is a fraction of 2^30.
 */
#include <stdbool.h>
#include <stdint.h>

#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h"  /* made of the `strings` folder by tools/perinkpheral */

#define CITY_KEY        "city"
#define HEADER_CITY_KEY "header_city"
#define REDRAW_TIMER    0
#define NOTIFY_ALARM    0
#define NOTIFIED_KEY    "notified"
#define RETRY_FILE      "notice-retry"
#define RETRY_TIMER     1

/* ---- arithmetic ---- */

#define UDEG          1000000LL          /* a degree, in the units of an angle */
#define FULL_TURN     (360 * UDEG)
#define HALF_TURN     (180 * UDEG)
#define QUARTER_TURN  (90 * UDEG)
#define ONE           (1LL << 30)        /* 1, as a sine is written */
#define UDAYS_PER_DAY 1000000LL          /* a day, in the units of time */
#define RADIAN_ONE    18740330LL         /* a degree in radians, times 2^30 */

#define SECONDS_PER_DAY    86400
#define SECONDS_PER_HOUR   3600
#define SECONDS_PER_MINUTE 60
#define MS_PER_SECOND      1000
#define J2000_UNIX         946728000LL   /* 2000-01-01 12:00 UTC */
#define SETTING_DECIMALS   4             /* a latitude as 55.7558 */

static int64_t turn(int64_t angle)
{
    angle %= FULL_TURN;
    return angle < 0 ? angle + FULL_TURN : angle;
}

/* The sine of an angle, as a fraction of ONE: a series to x^11 over a
 * quarter turn, which is right to some 1e-8. */
static int64_t sine(int64_t angle)
{
    angle = turn(angle);
    bool is_negative = angle >= HALF_TURN;
    if (is_negative) {
        angle -= HALF_TURN;
    }
    if (angle > QUARTER_TURN) {
        angle = HALF_TURN - angle;
    }
    int64_t x = angle * RADIAN_ONE / UDEG;
    int64_t x2 = x * x >> 30;
    /* x (1 - x^2/6 (1 - x^2/20 (1 - x^2/42 (1 - x^2/72 (1 - x^2/110))))) */
    int64_t t = ONE - x2 / 110;
    t = ONE - (x2 * t >> 30) / 72;
    t = ONE - (x2 * t >> 30) / 42;
    t = ONE - (x2 * t >> 30) / 20;
    t = ONE - (x2 * t >> 30) / 6;
    int64_t result = x * t >> 30;
    return is_negative ? -result : result;
}

static int64_t cosine(int64_t angle)
{
    return sine(angle + QUARTER_TURN);
}

/* The angle of half a turn at most whose cosine is `value`: the cosine
 * falls all the way, so halving the range finds it. */
static int64_t arc_cosine(int64_t value)
{
    int64_t low = 0, high = HALF_TURN;
    while (high - low > 1) {
        int64_t middle = (low + high) / 2;
        if (cosine(middle) > value) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

static int64_t square_root(uint64_t value)
{
    uint64_t root = 0, bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (int64_t)root;
}

/* An angle that turns at `rate` hundred-millionths of a degree a day,
 * `days` millionths of a day after it stood at `start`. The whole days
 * and the rest are taken apart, or the product would not fit. */
static int64_t angle_after(int64_t start, int64_t rate, int64_t days)
{
    int64_t whole = rate * (days / UDAYS_PER_DAY) % (FULL_TURN * 100);
    int64_t part = rate * (days % UDAYS_PER_DAY) / UDAYS_PER_DAY;
    return turn(start + (whole + part) / 100);
}

/* Millionths of a day after J2000 as a moment of the clock. */
static int64_t unix_time_of(int64_t days)
{
    return J2000_UNIX + days * SECONDS_PER_DAY / UDAYS_PER_DAY;
}

/* ---- the sun ---- */

typedef enum {
    SKY_USUAL,
    SKY_POLAR_DAY,   /* the sun does not set */
    SKY_POLAR_NIGHT, /* the sun does not rise */
} sky_t;

typedef struct {
    sky_t   sky;
    int64_t rise, set; /* moments of the clock, for SKY_USUAL */
} sun_day_t;

#define SUN_ANOMALY_START 357529100  /* the mean anomaly at J2000 */
#define SUN_ANOMALY_RATE  98560028   /* 0.98560028 degrees a day */
#define PERIHELION        102937200  /* the argument of the perihelion */
#define OBLIQUITY         23439700   /* of the ecliptic */
#define HORIZON           (-833000)  /* refraction and the sun's half-width */

/* Select the solar cycle nearest local noon, including across the date line. */
static sun_day_t sun_on(int64_t local_noon, int64_t latitude, int64_t longitude)
{
    /* The mean solar noon there, in days after J2000. */
    int64_t cycle = ((local_noon - J2000_UNIX) * UDAYS_PER_DAY / SECONDS_PER_DAY +
                     longitude / 360 + UDAYS_PER_DAY / 2) / UDAYS_PER_DAY;
    int64_t noon = cycle * UDAYS_PER_DAY - longitude / 360;
    int64_t anomaly = angle_after(SUN_ANOMALY_START, SUN_ANOMALY_RATE, noon);
    int64_t sin_anomaly = sine(anomaly);
    int64_t centre =
        (1914800 * sin_anomaly + 20000 * sine(2 * anomaly) + 300 * sine(3 * anomaly)) >> 30;
    int64_t ecliptic = turn(anomaly + centre + HALF_TURN + PERIHELION);
    /* The equation of time, in millionths of a day. */
    int64_t transit = noon + ((5300 * sin_anomaly - 6900 * sine(2 * ecliptic)) >> 30);

    int64_t sin_declination = sine(ecliptic) * sine(OBLIQUITY) >> 30;
    int64_t cos_declination = square_root((uint64_t)(ONE * ONE - sin_declination * sin_declination));
    int64_t over = sine(HORIZON) - (sine(latitude) * sin_declination >> 30);
    int64_t under = cosine(latitude) * cos_declination >> 30;
    sun_day_t sun = { .sky = SKY_USUAL };
    /* At a pole the sun goes round the horizon: up all day or never. */
    int64_t cos_hour_angle = under > 0 ? over * ONE / under : over < 0 ? -ONE : ONE;
    if (cos_hour_angle <= -ONE) {
        sun.sky = SKY_POLAR_DAY;
    } else if (cos_hour_angle >= ONE) {
        sun.sky = SKY_POLAR_NIGHT;
    } else {
        int64_t half_day = arc_cosine(cos_hour_angle) / 360;
        sun.rise = unix_time_of(transit - half_day);
        sun.set = unix_time_of(transit + half_day);
    }
    return sun;
}

/* ---- the moon ---- */

typedef struct {
    int64_t elongation;  /* from the sun: 0 new, half a turn full */
    int32_t illuminated; /* per cent of the disc */
} moon_t;

static moon_t moon_at(int64_t now)
{
    int64_t days = (now - J2000_UNIX) * UDAYS_PER_DAY / SECONDS_PER_DAY;
    int64_t elongation = angle_after(297850192, 1219074911, days);
    int64_t sun_anomaly = angle_after(357529109, SUN_ANOMALY_RATE, days);
    int64_t moon_anomaly = angle_after(134963396, 1306499295, days);
    int64_t phase_angle =
        HALF_TURN - elongation -
        ((6289000 * sine(moon_anomaly) - 2100000 * sine(sun_anomaly) +
          1274000 * sine(2 * elongation - moon_anomaly) + 658000 * sine(2 * elongation) +
          214000 * sine(2 * moon_anomaly) + 110000 * sine(elongation)) >>
         30);
    moon_t moon = {
        .elongation = turn(HALF_TURN - phase_angle),
        .illuminated = (int32_t)(((ONE + cosine(phase_angle)) * 100 + ONE) / (2 * ONE)),
    };
    return moon;
}

/* The four moments - new, first quarter, full, last quarter - are named
 * for the day around each, as calendars name them; between them the moon
 * is a crescent or gibbous, waxing or waning. */
static const int32_t MOMENTS[] = {
    STR_PHASE_NEW, STR_PHASE_FIRST_QUARTER, STR_PHASE_FULL, STR_PHASE_LAST_QUARTER,
};
static const int32_t BETWEEN[] = {
    STR_PHASE_WAXING_CRESCENT, STR_PHASE_WAXING_GIBBOUS, STR_PHASE_WANING_GIBBOUS,
    STR_PHASE_WANING_CRESCENT,
};
#define MOMENT_WINDOW 12190749 /* a day of the moon's way: half a day either side */

static const char *phase_name(const moon_t *moon)
{
    int64_t quarter = turn(moon->elongation + QUARTER_TURN / 2) / QUARTER_TURN;
    int64_t from_moment = moon->elongation - quarter * QUARTER_TURN;
    from_moment = from_moment > HALF_TURN ? from_moment - FULL_TURN : from_moment;
    if (from_moment < MOMENT_WINDOW / 2 && from_moment > -MOMENT_WINDOW / 2) {
        return app_string(MOMENTS[quarter % 4]);
    }
    return app_string(BETWEEN[moon->elongation / QUARTER_TURN]);
}

/* The moon as a picture of 22 cells, as the others are: the outline, and
 * ink where the sun does not reach. The light is on the right while it
 * waxes - in the north; the south sees it the other way round. */
#define MOON_CELLS  22
#define MOON_STRIDE ((MOON_CELLS + 7) / 8)
static uint8_t moon_picture[2 + MOON_CELLS * MOON_STRIDE];

/* Inside the disc: coordinates are doubled, so that the centre is a
 * whole number. */
static bool is_on_disc(int32_t x, int32_t y)
{
    return x * x + y * y < MOON_CELLS * MOON_CELLS;
}

static void draw_moon_picture(const moon_t *moon, bool is_south)
{
    for (int32_t i = 0; i < (int32_t)sizeof moon_picture; i++) {
        moon_picture[i] = 0;
    }
    moon_picture[0] = MOON_CELLS;
    moon_picture[1] = MOON_CELLS;
    bool    is_waxing = moon->elongation < HALF_TURN;
    int64_t cos_elongation = cosine(moon->elongation);
    for (int32_t row = 0; row < MOON_CELLS; row++) {
        int32_t y = 2 * row + 1 - MOON_CELLS;
        int64_t half_width = square_root((uint64_t)(MOON_CELLS * MOON_CELLS - y * y));
        int64_t terminator = half_width * cos_elongation >> 30;
        for (int32_t column = 0; column < MOON_CELLS; column++) {
            int32_t x = 2 * column + 1 - MOON_CELLS;
            if (!is_on_disc(x, y)) {
                continue;
            }
            bool is_edge = !is_on_disc(x - 2, y) || !is_on_disc(x + 2, y) ||
                           !is_on_disc(x, y - 2) || !is_on_disc(x, y + 2);
            int32_t seen_x = is_south ? -x : x;
            bool    is_lit = is_waxing ? seen_x > terminator : seen_x < -terminator;
            if (is_edge || !is_lit) {
                moon_picture[2 + row * MOON_STRIDE + column / 8] |= (uint8_t)(0x80 >> (column % 8));
            }
        }
    }
}

/* ---- the settings ---- */

typedef struct {
    int64_t latitude, longitude; /* millionths of a degree */
} place_t;

static bool read_place(place_t *place)
{
    int32_t latitude, longitude;
    if (!app_setting_number("lat", SETTING_DECIMALS, &latitude) ||
        !app_setting_number("lon", SETTING_DECIMALS, &longitude)) {
        return false;
    }
    place->latitude = (int64_t)latitude * 100;
    place->longitude = (int64_t)longitude * 100;
    return true;
}

/* The header's left side: the city, while its setting is on. The right is
 * Perinkpheral OS's own. */
static void show_header(void)
{
    char    city[HOST_HEADER_TEXT_MAX + 1];
    int32_t length = app_setting_place(CITY_KEY, city, sizeof city);
    if (app_setting_is_on(HEADER_CITY_KEY) && length > 0) {
        header_set_text(HOST_HEADER_LEFT, city, length);
    } else {
        header_set_text(HOST_HEADER_LEFT, " ", 1); /* a space: the side stays empty */
    }
}

/* ---- notifications ---- */

typedef struct {
    int64_t at[2]; /* first fields retain the original receipt format */
    int64_t revision;
} receipts_t;
static receipts_t receipts;
static bool receipts_ready, receipts_dirty;

static void save_receipts(void)
{
    receipts.revision++;
    receipts_dirty = kv_set(NOTIFIED_KEY, sizeof NOTIFIED_KEY - 1, &receipts, sizeof receipts) != HOST_OK;
    if (receipts_dirty) {
        file_write(RETRY_FILE, sizeof RETRY_FILE - 1, 0, &receipts, sizeof receipts);
    }
}

static bool load_receipts(void)
{
    receipts_t saved = {0}, retry = {0};
    int32_t length = kv_get(NOTIFIED_KEY, sizeof NOTIFIED_KEY - 1, &saved, sizeof saved);
    if (length < 0 && length != HOST_ERR_NOT_FOUND) { return false; }
    if (length != sizeof saved && length != sizeof saved.at) { saved = (receipts_t){0}; }
    length = file_read(RETRY_FILE, sizeof RETRY_FILE - 1, 0, &retry, sizeof retry);
    if (length < 0 && length != HOST_ERR_NOT_FOUND) { return false; }
    receipts_dirty = length == sizeof retry && retry.revision > saved.revision;
    receipts = receipts_dirty ? retry : saved;
    return true;
}

static void retry_alarm(void)
{
    if (alarm_set(NOTIFY_ALARM, SECONDS_PER_MINUTE) != HOST_OK) {
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_WARNING, "", 0);
        log_write(HOST_LOG_ERROR, "Notification alarm refused", 26);
        timer_set(RETRY_TIMER, SECONDS_PER_MINUTE * MS_PER_SECOND);
    } else {
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    }
}

/* Values belong to the manifest, even when the labels are translated. An
 * absent or unrecognized value is Off, including on an older installation. */
static int32_t lead_seconds(const char *key)
{
    static const char *const choices[] = {
        "At the time", "5 min", "15 min", "30 min", "1 h",
    };
    static const int32_t seconds[] = { 0, 300, 900, 1800, 3600 };
    char choice[16];
    app_setting(key, choice, sizeof choice);
    for (int32_t i = 0; i < 5; i++) {
        if (app_text_equal(choice, choices[i])) { return seconds[i]; }
    }
    return -1;
}

/* Calendar's notice: the clock in the large line, the event below it.
 * Perinkpheral OS supplies the same OK/BACK actions in either language. */
static bool notify_of(int32_t event, int64_t at)
{
    host_notice_t notice = { .icon = event == 0 ? HOST_ICON_SUNRISE : HOST_ICON_SUNSET };
    app_time_text(at, HOST_TIME_CLOCK, notice.first, sizeof notice.first);
    app_text_copy(notice.detail, sizeof notice.detail,
                  app_string(event == 0 ? STR_SUNRISE : STR_SUNSET));
    return notification_post(&notice) == HOST_OK;
}

/* Recompute the nearest events and the next local midnight. Yesterday is
 * included for solar cycles crossing midnight; tomorrow for a lead time
 * crossing midnight. Local date conversion respects daylight saving.
 * Like Calendar, allow five minutes of late delivery, and retry a full
 * queue after a minute. Only accepted events are remembered across unloads
 * and restarts, so an already queued notice is not posted twice. */
static void update_notifications(bool deliver)
{
    if (!receipts_ready) { receipts_ready = load_receipts(); }
    if (!receipts_ready) { retry_alarm(); return; }
    if (receipts_dirty) { save_receipts(); }
    int32_t lead[2] = { lead_seconds("rise_notice"), lead_seconds("set_notice") };
    int64_t now = time_now();
    place_t place;
    host_local_time_t date;
    if ((lead[0] < 0 && lead[1] < 0) || now == 0 || !read_place(&place) ||
        time_local(&date) != HOST_OK) {
        if (receipts_dirty) { retry_alarm(); }
        else {
            alarm_set(NOTIFY_ALARM, 0);
            header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
        }
        return;
    }

    int64_t next = 0;
    date.hour = date.minute = date.second = 0;
    date.day++;
    time_from_local(&date, &next);
    date.day -= 2;
    date.hour = 12;
    bool changed = false;
    for (int32_t day = 0; day < 3; day++, date.day++) {
        int64_t noon;
        if (time_from_local(&date, &noon) != HOST_OK) { continue; }
        sun_day_t sun = sun_on(noon, place.latitude, place.longitude);
        if (sun.sky != SKY_USUAL) { continue; }
        int64_t moments[2] = { sun.rise, sun.set };
        for (int32_t i = 0; i < 2; i++) {
            int64_t at = moments[i];
            if (lead[i] < 0 || at == receipts.at[i] || at + 5 * SECONDS_PER_MINUTE < now ||
                at - lead[i] < HOST_TIME_MIN || at > HOST_TIME_MAX) {
                continue;
            }
            int64_t due = at - lead[i];
            if (deliver && due <= now) {
                if (notify_of(i, at)) {
                    receipts.at[i] = at;
                    changed = true;
                    continue;
                }
                due = now + SECONDS_PER_MINUTE;
            }
            if (due <= now) { due = now + 1; }
            if (next == 0 || due < next) { next = due; }
        }
    }
    if (changed) {
        save_receipts();
    }
    if (receipts_dirty && (next == 0 || next > now + SECONDS_PER_MINUTE)) {
        next = now + SECONDS_PER_MINUTE;
    }
    if (next > now && next <= HOST_TIME_MAX) {
        if (alarm_set_at(NOTIFY_ALARM, next) != HOST_OK) { retry_alarm(); }
        else { header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0); }
    }
}

/* ---- the handlers ---- */

/* What is shown changes with the hour - the moon - and the day: the
 * picture is drawn anew at the turn of every hour. */
static void schedule_redraw(void)
{
    int64_t now = time_now();
    int32_t into_hour = now == 0 ? 0 : (int32_t)(now % SECONDS_PER_HOUR);
    timer_set(REDRAW_TIMER, (SECONDS_PER_HOUR - into_hour) * MS_PER_SECOND);
}

void app_on_load(void)
{
    receipts_ready = receipts_dirty = false;
    show_header();
}

void app_on_focus(void)
{
    update_notifications(false);
    show_header();
    schedule_redraw();
}

void app_on_timer(int32_t id)
{
    if (id == RETRY_TIMER) { update_notifications(true); return; }
    update_notifications(true);
    screen_redraw(HOST_REDRAW_NORMAL);
    schedule_redraw();
}

void app_on_terminate(void)
{
    if (receipts_dirty) { save_receipts(); }
    if (receipts_dirty) { retry_alarm(); }
}

void app_on_settings(void)
{
    update_notifications(false);
    show_header();
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* The clock or the time zone is another: so is the day. */
void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED || intent == HOST_INTENT_TIME_CHANGED) {
        update_notifications(false);
    }
    if (intent == HOST_INTENT_TIME_CHANGED) {
        screen_redraw(HOST_REDRAW_SOON);
        schedule_redraw();
    }
}

void app_on_installed(void)
{
    update_notifications(false);
}

void app_on_updated(void)
{
    update_notifications(false);
}

void app_on_alarm(int32_t id)
{
    if (id == NOTIFY_ALARM) { update_notifications(true); }
}

/* ---- the picture ---- */

#define CELLS      4
#define RULE_INSET 16
#define TEXT_MAX   48

static void clock_text(char *text, const sun_day_t *sun, int64_t moment)
{
    if (sun->sky != SKY_USUAL) {
        app_text_copy(text, TEXT_MAX, "--:--");
        return;
    }
    app_time_text(moment, HOST_TIME_CLOCK, text, TEXT_MAX);
}

static void length_text(char *text, const sun_day_t *sun)
{
    int64_t seconds = sun->sky == SKY_POLAR_DAY     ? SECONDS_PER_DAY
                      : sun->sky == SKY_POLAR_NIGHT ? 0
                                                    : sun->set - sun->rise;
    int64_t minutes = (seconds + SECONDS_PER_MINUTE / 2) / SECONDS_PER_MINUTE;
    const char           *pattern = app_string(STR_HOURS_MINUTES);
    host_text_argument_t  parts[] = {
        { .number = (int32_t)(minutes / 60) },
        { .number = (int32_t)(minutes % 60) },
    };
    text[text_fill(pattern, app_text_length(pattern), parts, 2, text, TEXT_MAX - 1)] = '\0';
}

void app_on_draw(void)
{
    place_t place;
    if (!read_place(&place)) {
        ui_draw_reason(HOST_FAILED_NOT_READY);
        return;
    }
    int64_t now = time_now();
    if (now == 0) {
        ui_draw_reason(HOST_FAILED_NO_NETWORK); /* the time comes with the network */
        return;
    }
    int32_t offset = time_zone_offset();
    int64_t today = (now + offset) / SECONDS_PER_DAY;
    int64_t local_noon = today * SECONDS_PER_DAY + 12 * SECONDS_PER_HOUR - offset;
    sun_day_t sun = sun_on(local_noon, place.latitude, place.longitude);
    moon_t    moon = moon_at(now);
    draw_moon_picture(&moon, place.latitude < 0);

    int32_t     width = gfx_width() / 2, height = gfx_height() / 2;
    host_cell_t cells[CELLS];
    /* the sky is Perinkpheral OS's icons; the moon is drawn here, as it is tonight */
    const int32_t  icons[CELLS] = { HOST_ICON_SUNRISE, HOST_ICON_SUNSET, HOST_ICON_SUN, HOST_ICON_NONE };
    const uint8_t *pictures[CELLS] = { 0, 0, 0, moon_picture };
    for (int32_t i = 0; i < CELLS; i++) {
        cells[i] = (host_cell_t){ .x = i % 2 * width, .y = i / 2 * height, .width = width,
                                  .height = height };
    }
    app_text_copy(cells[0].name, TEXT_MAX, app_string(STR_SUNRISE));
    clock_text(cells[0].value, &sun, sun.rise);
    app_text_copy(cells[1].name, TEXT_MAX, app_string(STR_SUNSET));
    clock_text(cells[1].value, &sun, sun.set);
    app_text_copy(cells[2].name, TEXT_MAX, app_string(STR_DAY_LENGTH));
    length_text(cells[2].value, &sun);
    app_text_copy(cells[3].name, TEXT_MAX, phase_name(&moon));
    app_text_t value;
    app_text_start(&value, cells[3].value, TEXT_MAX);
    app_text_add_number(&value, moon.illuminated);
    app_text_add(&value, "%");

    app_draw_cells(cells, pictures, icons, CELLS); /* one size for all of them */
    gfx_draw_rect(width, RULE_INSET, 1, gfx_height() - 2 * RULE_INSET, HOST_RECT_FILLED);
    gfx_draw_rect(RULE_INSET, height, gfx_width() - 2 * RULE_INSET, 1, HOST_RECT_FILLED);
}
