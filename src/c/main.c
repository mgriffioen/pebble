/*
 * simple-watchface — a Pebble watchface in C
 *
 * KEY CONCEPTS DEMONSTRATED
 * ─────────────────────────
 * 1. Window lifecycle  (create → load → unload → destroy)
 * 2. TextLayer         (the primary way to draw text)
 * 3. TickTimerService  (called every minute to update the clock)
 * 4. BatteryStateService (reports remaining charge)
 * 5. ConnectionService (reports Bluetooth link to the phone)
 * 6. Proper memory management (every create has a matching destroy)
 *
 * LAYOUT  (144 × 168 px on Basalt / Diorite; 180 × 180 on Chalk)
 * ──────────────────────────────────────────────────────────────
 *   ┌──────────────────────────────┐
 *   │ BT            BAT %          │  ← status bar (top)
 *   │                              │
 *   │          HH : MM             │  ← time (large, centred)
 *   │        Day, Mon DD           │  ← date (smaller, below)
 *   │                              │
 *   └──────────────────────────────┘
 */

#include <pebble.h>

/* ── module-level state ──────────────────────────────────────────────── */

static Window    *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_bt_layer;

/* ── helpers ─────────────────────────────────────────────────────────── */

/*
 * update_time — format and display the current time and date.
 *
 * Called once on startup and then by tick_handler every minute.
 * We use static char buffers so the strings live as long as the layer
 * references them (TextLayer only stores a pointer, not a copy).
 */
static void update_time(struct tm *tick_time) {
  static char s_time_buf[8];   /* "HH:MM\0" → 6 chars, 8 for safety */
  static char s_date_buf[32];  /* "Wednesday, January 31\0"           */

  /* Respect the user's 12 h / 24 h system preference. */
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", tick_time);
  }

  /* "%e" gives a space-padded day number (no leading zero). */
  strftime(s_date_buf, sizeof(s_date_buf), "%A, %B %e", tick_time);

  text_layer_set_text(s_time_layer, s_time_buf);
  text_layer_set_text(s_date_layer, s_date_buf);
}

/* ── service callbacks ───────────────────────────────────────────────── */

/*
 * tick_handler — fired by the OS every MINUTE_UNIT.
 *
 * Pebble passes a pre-computed `struct tm *` so you don't have to
 * call localtime() yourself each tick.
 */
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
}

/*
 * battery_handler — fired whenever battery state changes.
 *
 * BatteryChargeState contains:
 *   .charge_percent  (0–100, rounded to 10)
 *   .is_charging     (true while on charger)
 *   .is_plugged      (true if USB cable attached)
 */
static void battery_handler(BatteryChargeState state) {
  static char s_battery_buf[16];
  if (state.is_charging) {
    snprintf(s_battery_buf, sizeof(s_battery_buf), "CHG %d%%", state.charge_percent);
  } else {
    snprintf(s_battery_buf, sizeof(s_battery_buf), "BAT %d%%", state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, s_battery_buf);
}

/*
 * bluetooth_handler — fired when the Pebble connects or disconnects
 * from the companion phone app.
 *
 * We vibrate briefly on disconnect so the user notices they walked
 * out of range.
 */
static void bluetooth_handler(bool connected) {
  text_layer_set_text(s_bt_layer, connected ? "BT" : "--");
  if (!connected) {
    vibes_double_pulse();
  }
}

/* ── window lifecycle ────────────────────────────────────────────────── */

/*
 * main_window_load — called after the window is pushed onto the stack.
 *
 * This is where you create all child layers.  Do NOT create layers in
 * init(); the window root layer doesn't exist yet at that point.
 *
 * GRect(x, y, width, height) — Pebble's rectangle type.
 * Layer coordinates are relative to the parent layer's origin.
 */
static void main_window_load(Window *window) {
  Layer  *root   = window_get_root_layer(window);
  GRect   bounds = layer_get_bounds(root);

  int w = bounds.size.w;
  int h = bounds.size.h;

  /* Dark background. */
  window_set_background_color(window, GColorBlack);

  /* ── Time layer ────────────────────────────────────────────────────
   * Vertically centred, occupies 60 px of height.
   * FONT_KEY_BITHAM_42_BOLD is the large system font (≈42 px tall).
   */
  s_time_layer = text_layer_create(GRect(0, h / 2 - 38, w, 60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  /* ── Date layer ────────────────────────────────────────────────────
   * Sits just below the time layer.
   */
  s_date_layer = text_layer_create(GRect(0, h / 2 + 26, w, 30));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorLightGray);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  /* ── Battery layer (top-right) ──────────────────────────────────── */
  s_battery_layer = text_layer_create(GRect(w - 64, 4, 60, 18));
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_text_color(s_battery_layer, GColorDarkGray);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
  layer_add_child(root, text_layer_get_layer(s_battery_layer));

  /* ── Bluetooth layer (top-left) ─────────────────────────────────── */
  s_bt_layer = text_layer_create(GRect(4, 4, 30, 18));
  text_layer_set_background_color(s_bt_layer, GColorClear);
  text_layer_set_text_color(s_bt_layer, GColorDarkGray);
  text_layer_set_font(s_bt_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_bt_layer, GTextAlignmentLeft);
  layer_add_child(root, text_layer_get_layer(s_bt_layer));

  /* ── Initial values ─────────────────────────────────────────────────
   * The tick / battery / BT callbacks won't fire until something
   * actually changes, so we must populate the layers manually here.
   */
  time_t now = time(NULL);
  update_time(localtime(&now));

  battery_handler(battery_state_service_peek());

  bluetooth_handler(connection_service_peek_pebble_app_connection());
}

/*
 * main_window_unload — called just before the window is popped off the
 * stack (e.g. watchface dismissed).
 *
 * Destroy every layer created in load().  Failure to do so leaks heap
 * memory — Pebble watches have only ~24 KB of app heap.
 */
static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_bt_layer);
}

/* ── app lifecycle ───────────────────────────────────────────────────── */

/*
 * init — called once when the OS launches the watchface.
 *
 * Create the window, register handlers, then push the window.
 * Pushing the window triggers the .load callback defined above.
 */
static void init(void) {
  s_main_window = window_create();

  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load   = main_window_load,
    .unload = main_window_unload,
  });

  /* animated = true → slide-in animation when pushing */
  window_stack_push(s_main_window, true);

  /* Subscribe to the tick timer — MINUTE_UNIT fires every minute.
   * Other options: SECOND_UNIT, HOUR_UNIT, DAY_UNIT, etc.          */
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  /* Subscribe to battery and Bluetooth state changes. */
  battery_state_service_subscribe(battery_handler);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_handler,
  });
}

/*
 * deinit — called when the watchface is about to be unloaded.
 *
 * Unsubscribe from every service and destroy the window.
 * The window's .unload callback will fire and clean up the layers.
 */
static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();

  window_destroy(s_main_window);
}

/*
 * main — the OS entry point.
 *
 * init()            → set everything up
 * app_event_loop()  → hand control back to the OS event loop
 *                     (this function blocks until the app exits)
 * deinit()          → tear everything down
 */
int main(void) {
  init();
  app_event_loop();
  deinit();
}
