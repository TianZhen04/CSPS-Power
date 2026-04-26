#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <display.h>
#include <key.h>
#include <ui.h>

static constexpr uint16_t kScreenWidth = 240;
static constexpr uint16_t kScreenHeight = 240;
static constexpr uint8_t kRotation = 0;
static constexpr bool kBacklightActiveHigh = true;
static constexpr uint8_t kBacklightPwmChannel = 0;
static constexpr uint8_t kBacklightPwmResolutionBits = 8;
static constexpr uint32_t kBacklightPwmFreqHz = 5000;
static constexpr const char *kDisplayPrefsNamespace = "disp_cfg";
static constexpr const char *kRotationKey = "rotation";
static constexpr const char *kBrightnessKey = "brightness";
// 屏幕亮度 0-255，数值越大越亮
static constexpr uint8_t kDefaultBrightness = 64;

static TFT_eSPI tft = TFT_eSPI(kScreenWidth, kScreenHeight);
static uint8_t g_rotation = kRotation;
static uint8_t g_brightness = kDefaultBrightness;
static Preferences g_display_prefs;
static bool g_display_prefs_ready = false;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[kScreenWidth * 10];

static uint8_t normalize_rotation(uint8_t rotation)
{
  return static_cast<uint8_t>(rotation % 4U);
}

static void display_save_u8(const char *key, uint8_t value)
{
  if (!g_display_prefs_ready || key == NULL)
  {
    return;
  }

  g_display_prefs.putUChar(key, value);
}

static void backlight_apply_raw(uint8_t brightness)
{
  const uint8_t duty = kBacklightActiveHigh ? brightness : static_cast<uint8_t>(255 - brightness);
  ledcWrite(kBacklightPwmChannel, duty);
}

static void display_load_settings()
{
  g_display_prefs_ready = g_display_prefs.begin(kDisplayPrefsNamespace, false);
  if (!g_display_prefs_ready)
  {
    g_rotation = kRotation;
    g_brightness = kDefaultBrightness;
    return;
  }

  g_rotation = normalize_rotation(g_display_prefs.getUChar(kRotationKey, kRotation));
  g_brightness = g_display_prefs.getUChar(kBrightnessKey, kDefaultBrightness);
}

static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
  const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors(reinterpret_cast<uint16_t *>(&color_p->full), w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp_drv);
}

void backlight_set(uint8_t brightness)
{
  display_set_brightness(brightness);
}

void display_set_brightness(uint8_t brightness)
{
  g_brightness = brightness;
  backlight_apply_raw(g_brightness);
  display_save_u8(kBrightnessKey, g_brightness);
}

uint8_t display_get_brightness()
{
  return g_brightness;
}

void display_set_rotation(uint8_t rotation)
{
  g_rotation = normalize_rotation(rotation);
  tft.setRotation(g_rotation);
  display_save_u8(kRotationKey, g_rotation);

  // Rotation remaps pixel coordinates; force a full refresh to avoid stale regions.
  tft.fillScreen(TFT_BLACK);
  lv_disp_t *disp = lv_disp_get_default();
  if (disp != NULL)
  {
    lv_obj_t *active = lv_scr_act();
    if (active != NULL)
    {
      lv_obj_invalidate(active);
    }
    lv_refr_now(disp);
  }
}

uint8_t display_get_rotation()
{
  return g_rotation;
}

static void backlight_init()
{
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, kBacklightActiveHigh ? LOW : HIGH);

  ledcSetup(kBacklightPwmChannel, kBacklightPwmFreqHz, kBacklightPwmResolutionBits);
  ledcAttachPin(TFT_BL, kBacklightPwmChannel);
  backlight_apply_raw(g_brightness);
}

void display_init()
{
  lv_init();
  display_load_settings();

  tft.begin();
  display_set_rotation(g_rotation);
  backlight_init();
  tft.fillScreen(TFT_BLACK);

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, kScreenWidth * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = kScreenWidth;
  disp_drv.ver_res = kScreenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  key_init();
  ui_init();

  lv_indev_t *keypad_indev = get_keypad_indev();
  lv_group_t *ui_group = ui_get_input_group();
  if (keypad_indev != NULL && ui_group != NULL)
  {
    lv_indev_set_group(keypad_indev, ui_group);
  }

  lv_timer_handler();
}

void display_task_handler()
{
  lv_tick_inc(5);
  const uint32_t wait_ms = lv_timer_handler();
  delay(wait_ms > 20 ? 20 : wait_ms);
}
