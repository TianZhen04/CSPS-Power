#include <ui.h>
#include <Esp.h>
#include <string.h>
#include <key.h>
#include <ble_server.h>
#include <beep.h>
#include <display.h>
#include <power_ctrl.h>
#include <wifi_portal.h>

extern "C"
{
#include "../UI/screens/ui_Screen1.h"
#include "../UI/screens/ui_Screen2.h"
#include "../UI/screens/ui_Screen3.h"
#include "../UI/screens/ui_Screen4.h"
extern const lv_img_dsc_t ui_img_546246141;
extern const lv_img_dsc_t ui_img_808252844;
extern const lv_font_t ui_font_bold16;
}

static lv_group_t *g_screen1_group = NULL;
static lv_group_t *g_screen2_group = NULL;
static lv_group_t *g_screen3_group = NULL;
static lv_group_t *g_screen4_group = NULL;
static lv_group_t *g_active_group = NULL;
static bool g_screen3_ap_prompt = false;
static bool g_screen3_cleared = false;
static bool g_syncing_pson_switch = false;
static bool g_syncing_forced_switch = false;
static bool g_syncing_beep_switch = false;
static bool g_syncing_bluetooth_switch = false;
static lv_obj_t *g_force_off_popup = NULL;
static lv_timer_t *g_force_off_popup_timer = NULL;
static float g_board_temp_c = 0.0f;

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
	if (label == NULL || text == NULL)
	{
		return;
	}

	const char *current = lv_label_get_text(label);
	if (current != NULL && strcmp(current, text) == 0)
	{
		return;
	}

	lv_label_set_text(label, text);
}

static void set_bg_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
	if (obj == NULL)
	{
		return;
	}

	const lv_color_t current = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
	if (current.full == color.full)
	{
		return;
	}

	lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void update_screen2_rom_info()
{
	if (ui_rom == NULL)
	{
		return;
	}

	const uint32_t free_bytes = ESP.getFreeSketchSpace();
	char buf[24];

	if (free_bytes >= (1024U * 1024U))
	{
		const uint32_t whole_mb = free_bytes / (1024U * 1024U);
		const uint32_t tenth_mb = (free_bytes % (1024U * 1024U)) * 10U / (1024U * 1024U);
		lv_snprintf(buf, sizeof(buf), "ROM:%u.%uM", static_cast<unsigned>(whole_mb), static_cast<unsigned>(tenth_mb));
	}
	else
	{
		const uint32_t free_kb = free_bytes / 1024U;
		lv_snprintf(buf, sizeof(buf), "ROM:%uK", static_cast<unsigned>(free_kb));
	}

	set_label_text_if_changed(ui_rom, buf);
}

static void set_active_group(lv_group_t *group)
{
	if (group == NULL)
	{
		return;
	}

	g_active_group = group;
	lv_group_set_default(group);

	lv_indev_t *keypad_indev = get_keypad_indev();
	if (keypad_indev != NULL)
	{
		lv_indev_set_group(keypad_indev, group);
	}
}

static void add_obj_to_group(lv_group_t *group, lv_obj_t *obj)
{
	if (group == NULL || obj == NULL)
	{
		return;
	}

	/* Ensure object can receive ENTER as CLICK from keypad focus. */
	lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
	lv_group_add_obj(group, obj);
}

static bool is_activate_event(lv_event_t *e)
{
	// For keypad focus + clickable objects, ENTER will also emit CLICKED.
	// Handle only CLICKED to avoid one press triggering twice.
	return lv_event_get_code(e) == LV_EVENT_CLICKED;
}

static void on_setting_enter(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen2 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen2);
	set_active_group(g_screen2_group);
	if (ui_wifiSetting != NULL)
	{
		lv_group_focus_obj(ui_wifiSetting);
	}
}

static void on_return_to_main(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen1 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen1);
	set_active_group(g_screen1_group);
	if (ui_setting != NULL)
	{
		lv_group_focus_obj(ui_setting);
	}
}

static void update_screen3_wifi_info()
{
	if (ui_Label20 == NULL || ui_Label24 == NULL || ui_Label25 == NULL || ui_wifiState == NULL)
	{
		return;
	}

	set_label_text_if_changed(ui_Label25, "UNKNOWN");

	String ssid_text = "UNKNOWN";
	String ip_text = "UNKNOWN";
	const char *state_text = "DISCONNECTED";
	lv_color_t panel_color = lv_palette_main(LV_PALETTE_RED);

	if (wifi_portal_sta_connected())
	{
		ssid_text = wifi_portal_get_sta_ssid();
		ip_text = wifi_portal_get_sta_ip();
		state_text = "CONNECTED";
		panel_color = lv_palette_main(LV_PALETTE_GREEN);
	}
	else if (g_screen3_cleared)
	{
		state_text = "CLEARED";
		panel_color = lv_palette_main(LV_PALETTE_GREY);
	}
	else if (g_screen3_ap_prompt)
	{
		ssid_text = wifi_portal_get_ap_ssid();
		ip_text = wifi_portal_get_ap_ip();
		state_text = "CONNECT TO AP";
		panel_color = lv_palette_main(LV_PALETTE_ORANGE);
	}
	else if (!wifi_portal_has_sta_config() && wifi_portal_ap_active())
	{
		ssid_text = wifi_portal_get_ap_ssid();
		ip_text = wifi_portal_get_ap_ip();
		state_text = "AP READY";
		panel_color = lv_palette_main(LV_PALETTE_ORANGE);
	}

	set_label_text_if_changed(ui_Label20, ssid_text.c_str());
	set_label_text_if_changed(ui_Label24, ip_text.c_str());
	set_label_text_if_changed(ui_wifiState, state_text);
	set_bg_color_if_changed(ui_Panel5, panel_color);
	return;

	// TIME per requirement: show unknown in Wi-Fi setup workflow.
	lv_label_set_text(ui_Label25, "未知");

	if (wifi_portal_sta_connected())
	{
		lv_label_set_text(ui_Label20, wifi_portal_get_sta_ssid().c_str());
		lv_label_set_text(ui_Label24, wifi_portal_get_sta_ip().c_str());
		lv_label_set_text(ui_wifiState, "已连接");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		return;
	}

	if (g_screen3_cleared)
	{
		lv_label_set_text(ui_Label20, "未知");
		lv_label_set_text(ui_Label24, "未知");
		lv_label_set_text(ui_wifiState, "未知");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		return;
	}

	if (g_screen3_ap_prompt)
	{
		lv_label_set_text(ui_Label20, wifi_portal_get_ap_ssid().c_str());
		lv_label_set_text(ui_Label24, wifi_portal_get_ap_ip().c_str());
		lv_label_set_text(ui_wifiState, "请连接至热点");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		return;
	}

	if (!wifi_portal_has_sta_config() && wifi_portal_ap_active())
	{
		lv_label_set_text(ui_Label20, wifi_portal_get_ap_ssid().c_str());
		lv_label_set_text(ui_Label24, wifi_portal_get_ap_ip().c_str());
		lv_label_set_text(ui_wifiState, "AP READY");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		return;
	}

	if (wifi_portal_has_sta_config())
	{
		lv_label_set_text(ui_Label20, "未知");
		lv_label_set_text(ui_Label24, "未知");
		lv_label_set_text(ui_wifiState, "未连接");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
	}
	else
	{
		lv_label_set_text(ui_Label20, "未知");
		lv_label_set_text(ui_Label24, "未知");
		lv_label_set_text(ui_wifiState, "未连接");
		if (ui_Panel5 != NULL)
		{
			lv_obj_set_style_bg_color(ui_Panel5, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
	}
}

static void on_wifi_setting_enter(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen3 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen3);
	set_active_group(g_screen3_group);
	update_screen3_wifi_info();
	if (ui_openWifi != NULL)
	{
		lv_group_focus_obj(ui_openWifi);
	}
}

static void on_system_setting_enter(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen4 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen4);
	set_active_group(g_screen4_group);
	if (ui_forcedSwitch != NULL)
	{
		lv_group_focus_obj(ui_forcedSwitch);
	}
}

static void on_return_to_screen2(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen2 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen2);
	set_active_group(g_screen2_group);
	if (ui_wifiSetting != NULL)
	{
		lv_group_focus_obj(ui_wifiSetting);
	}
}

static void on_return3_to_screen2(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	if (ui_Screen2 == NULL)
	{
		return;
	}

	lv_disp_load_scr(ui_Screen2);
	set_active_group(g_screen2_group);
	if (ui_systemSetting != NULL)
	{
		lv_group_focus_obj(ui_systemSetting);
	}
}

static void on_open_wifi(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	wifi_portal_start();
	g_screen3_ap_prompt = true;
	g_screen3_cleared = false;
	update_screen3_wifi_info();
}

static void on_clean_wifi(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	wifi_portal_clear_config();
	wifi_portal_start();
	g_screen3_ap_prompt = false;
	g_screen3_cleared = true;
	update_screen3_wifi_info();
}

static void apply_brightness_from_slider()
{
	if (ui_brightNess == NULL)
	{
		return;
	}

	const int value = lv_slider_get_value(ui_brightNess);
	const int clamped = value < 0 ? 0 : (value > 100 ? 100 : value);
	const uint8_t brightness = static_cast<uint8_t>((clamped * 255) / 100);
	display_set_brightness(brightness);
}

static void sync_brightness_slider_from_display()
{
	if (ui_brightNess == NULL)
	{
		return;
	}

	const uint8_t brightness = display_get_brightness();
	const int slider_value = static_cast<int>((static_cast<uint32_t>(brightness) * 100U + 127U) / 255U);
	lv_slider_set_value(ui_brightNess, slider_value, LV_ANIM_OFF);
}

static void on_brightness_changed(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
	{
		return;
	}

	apply_brightness_from_slider();
}

static void rotate_display_step(int8_t step)
{
	const int current = static_cast<int>(display_get_rotation());
	int next = current + static_cast<int>(step);
	if (next < 0)
	{
		next += 4;
	}
	next %= 4;
	display_set_rotation(static_cast<uint8_t>(next));
}

static void on_rotate_add(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	rotate_display_step(1);
}

static void on_rotate_reduce(lv_event_t *e)
{
	if (!is_activate_event(e))
	{
		return;
	}

	rotate_display_step(-1);
}

static void close_force_off_popup()
{
	if (g_force_off_popup != NULL)
	{
		lv_obj_del_async(g_force_off_popup);
		g_force_off_popup = NULL;
	}

	if (g_force_off_popup_timer != NULL)
	{
		lv_timer_del(g_force_off_popup_timer);
		g_force_off_popup_timer = NULL;
	}
}

static void force_off_popup_timer_cb(lv_timer_t *timer)
{
	if (g_force_off_popup != NULL)
	{
		lv_obj_del_async(g_force_off_popup);
		g_force_off_popup = NULL;
	}

	if (g_force_off_popup_timer == timer)
	{
		g_force_off_popup_timer = NULL;
	}
}

static void show_force_off_popup()
{
	if (g_force_off_popup == NULL)
	{
		lv_obj_t *parent = lv_layer_top();
		g_force_off_popup = lv_obj_create(parent);
		lv_obj_set_size(g_force_off_popup, 150, 60);
		lv_obj_center(g_force_off_popup);
		lv_obj_clear_flag(g_force_off_popup, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_bg_color(g_force_off_popup, lv_color_hex(0x202020), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_opa(g_force_off_popup, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_border_color(g_force_off_popup, lv_color_hex(0xFF5A5A), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_border_width(g_force_off_popup, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_radius(g_force_off_popup, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

		lv_obj_t *label = lv_label_create(g_force_off_popup);
		lv_label_set_text(label, "Forced Off");
		lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_center(label);
	}

	if (g_force_off_popup_timer == NULL)
	{
		g_force_off_popup_timer = lv_timer_create(force_off_popup_timer_cb, 500, NULL);
		lv_timer_set_repeat_count(g_force_off_popup_timer, 1);
	}
	else
	{
		lv_timer_reset(g_force_off_popup_timer);
	}
}

// ── Shutdown popup (dismiss with SWITCH_ENTER) ──
static lv_obj_t *g_shutdown_popup = NULL;
static lv_obj_t *g_shutdown_popup_label = NULL;
static lv_group_t *g_shutdown_popup_group = NULL;

static void close_shutdown_popup()
{
	if (g_shutdown_popup_group != NULL)
	{
		lv_group_del(g_shutdown_popup_group);
		g_shutdown_popup_group = NULL;
	}
	if (g_shutdown_popup != NULL)
	{
		lv_obj_del_async(g_shutdown_popup);
		g_shutdown_popup = NULL;
		g_shutdown_popup_label = NULL;
	}
	// Restore previous active group
	set_active_group(g_active_group);
}

static void on_shutdown_popup_click(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
	{
		return;
	}
	close_shutdown_popup();
}

void ui_show_shutdown_popup(const char *reason)
{
	if (g_shutdown_popup != NULL)
	{
		// Already showing, update reason text
		if (g_shutdown_popup_label != NULL && reason != NULL && reason[0] != '\0')
		{
			set_label_text_if_changed(g_shutdown_popup_label, reason);
		}
		return;
	}

	lv_obj_t *parent = lv_layer_top();
	g_shutdown_popup = lv_obj_create(parent);
	lv_obj_set_size(g_shutdown_popup, 200, 100);
	lv_obj_center(g_shutdown_popup);
	lv_obj_clear_flag(g_shutdown_popup, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(g_shutdown_popup, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_color(g_shutdown_popup, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(g_shutdown_popup, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(g_shutdown_popup, lv_color_hex(0xFF4444), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(g_shutdown_popup, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(g_shutdown_popup, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

	// Title label
	lv_obj_t *title = lv_label_create(g_shutdown_popup);
	lv_label_set_text(title, "Power Abnormal Shutdown");
	lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(title, &ui_font_bold16, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

	// Reason label
	g_shutdown_popup_label = lv_label_create(g_shutdown_popup);
	const char *text = (reason != NULL && reason[0] != '\0') ? reason : "Unknown";
	lv_label_set_text(g_shutdown_popup_label, text);
	lv_obj_set_style_text_color(g_shutdown_popup_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(g_shutdown_popup_label, &ui_font_bold16, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(g_shutdown_popup_label, LV_ALIGN_CENTER, 0, 6);

	// Dismiss hint
	lv_obj_t *hint = lv_label_create(g_shutdown_popup);
	lv_label_set_text(hint, "[ENTER] to dismiss");
	lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

	// Create a group for the popup so it can receive key events
	g_shutdown_popup_group = lv_group_create();
	lv_group_add_obj(g_shutdown_popup_group, g_shutdown_popup);
	lv_group_set_default(g_shutdown_popup_group);

	lv_indev_t *keypad_indev = get_keypad_indev();
	if (keypad_indev != NULL)
	{
		lv_indev_set_group(keypad_indev, g_shutdown_popup_group);
	}

	lv_obj_add_event_cb(g_shutdown_popup, on_shutdown_popup_click, LV_EVENT_CLICKED, NULL);

	Serial.printf("UI: shutdown popup shown, reason=%s\n", text);
}

void ui_dismiss_shutdown_popup()
{
	if (g_shutdown_popup != NULL)
	{
		close_shutdown_popup();
	}
}

// ── Warning popup (10s auto-dismiss) ──
static lv_obj_t *g_warning_popup = NULL;
static lv_obj_t *g_warning_popup_label = NULL;
static lv_timer_t *g_warning_popup_timer = NULL;
static lv_group_t *g_warning_popup_group = NULL;

static void close_warning_popup()
{
	if (g_warning_popup_timer != NULL)
	{
		lv_timer_del(g_warning_popup_timer);
		g_warning_popup_timer = NULL;
	}
	if (g_warning_popup_group != NULL)
	{
		lv_group_del(g_warning_popup_group);
		g_warning_popup_group = NULL;
	}
	if (g_warning_popup != NULL)
	{
		lv_obj_del_async(g_warning_popup);
		g_warning_popup = NULL;
		g_warning_popup_label = NULL;
	}
	set_active_group(g_active_group);
}

static void warning_popup_timer_cb(lv_timer_t *timer)
{
	close_warning_popup();
}

static void on_warning_popup_click(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
	{
		return;
	}
	close_warning_popup();
}

void ui_show_warning_popup(const char *reason)
{
	if (g_warning_popup != NULL)
	{
		// Already showing, reset timer and update reason
		if (g_warning_popup_timer != NULL)
		{
			lv_timer_reset(g_warning_popup_timer);
		}
		if (g_warning_popup_label != NULL && reason != NULL && reason[0] != '\0')
		{
			set_label_text_if_changed(g_warning_popup_label, reason);
		}
		return;
	}

	lv_obj_t *parent = lv_layer_top();
	g_warning_popup = lv_obj_create(parent);
	lv_obj_set_size(g_warning_popup, 200, 100);
	lv_obj_center(g_warning_popup);
	lv_obj_clear_flag(g_warning_popup, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(g_warning_popup, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_color(g_warning_popup, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(g_warning_popup, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(g_warning_popup, lv_color_hex(0xF0A020), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(g_warning_popup, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(g_warning_popup, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

	// Title label
	lv_obj_t *title = lv_label_create(g_warning_popup);
	lv_label_set_text(title, "Power Warning");
	lv_obj_set_style_text_color(title, lv_color_hex(0xF0A020), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(title, &ui_font_bold16, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

	// Reason label
	g_warning_popup_label = lv_label_create(g_warning_popup);
	const char *text = (reason != NULL && reason[0] != '\0') ? reason : "Unknown";
	lv_label_set_text(g_warning_popup_label, text);
	lv_obj_set_style_text_color(g_warning_popup_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(g_warning_popup_label, &ui_font_bold16, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(g_warning_popup_label, LV_ALIGN_CENTER, 0, 6);

	// Dismiss hint
	lv_obj_t *hint = lv_label_create(g_warning_popup);
	lv_label_set_text(hint, "Auto-dismiss in 10s");
	lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

	// Create a group for the popup so it can receive key events
	g_warning_popup_group = lv_group_create();
	lv_group_add_obj(g_warning_popup_group, g_warning_popup);
	lv_group_set_default(g_warning_popup_group);

	lv_indev_t *keypad_indev = get_keypad_indev();
	if (keypad_indev != NULL)
	{
		lv_indev_set_group(keypad_indev, g_warning_popup_group);
	}

	lv_obj_add_event_cb(g_warning_popup, on_warning_popup_click, LV_EVENT_CLICKED, NULL);

	// Auto-dismiss after 10 seconds
	g_warning_popup_timer = lv_timer_create(warning_popup_timer_cb, 10000, NULL);
	lv_timer_set_repeat_count(g_warning_popup_timer, 1);

	Serial.printf("UI: warning popup shown, reason=%s\n", text);
}

// ── Warning icon control ──
static lv_timer_t *g_warning_icon_timer = NULL;
static bool g_warning_icon_visible = false;

static void warning_icon_timer_cb(lv_timer_t *timer)
{
	if (ui_warning == NULL)
	{
		return;
	}

	if (g_warning_icon_visible)
	{
		lv_obj_add_flag(ui_warning, LV_OBJ_FLAG_HIDDEN);
		g_warning_icon_visible = false;
	}
	else
	{
		lv_obj_clear_flag(ui_warning, LV_OBJ_FLAG_HIDDEN);
		g_warning_icon_visible = true;
	}
}

void ui_set_warning_icon_visible(bool visible)
{
	if (ui_warning == NULL)
	{
		return;
	}

	if (visible)
	{
		// Start flashing timer if not already running
		if (g_warning_icon_timer == NULL)
		{
			g_warning_icon_timer = lv_timer_create(warning_icon_timer_cb, 500, NULL);
			lv_timer_set_repeat_count(g_warning_icon_timer, -1);
		}
	}
	else
	{
		// Stop timer and hide icon
		if (g_warning_icon_timer != NULL)
		{
			lv_timer_del(g_warning_icon_timer);
			g_warning_icon_timer = NULL;
		}
		lv_obj_add_flag(ui_warning, LV_OBJ_FLAG_HIDDEN);
		g_warning_icon_visible = false;
	}
}

void ui_warning_task()
{
	// No periodic work needed currently; timers handle auto-dismiss and flashing.
}

static void sync_pson_switch_from_power_ctrl()
{
	if (ui_PSONSwitch == NULL)
	{
		return;
	}

	const bool should_check = power_ctrl_software_enabled();
	const bool is_checked = lv_obj_has_state(ui_PSONSwitch, LV_STATE_CHECKED);
	if (should_check == is_checked)
	{
		return;
	}

	g_syncing_pson_switch = true;
	if (should_check)
	{
		lv_obj_add_state(ui_PSONSwitch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(ui_PSONSwitch, LV_STATE_CHECKED);
	}
	g_syncing_pson_switch = false;
}

static void sync_forced_switch_from_power_ctrl()
{
	if (ui_forcedSwitch == NULL)
	{
		return;
	}

	const bool should_check = !power_ctrl_force_enabled();
	const bool is_checked = lv_obj_has_state(ui_forcedSwitch, LV_STATE_CHECKED);
	if (should_check == is_checked)
	{
		return;
	}

	g_syncing_forced_switch = true;
	if (should_check)
	{
		lv_obj_add_state(ui_forcedSwitch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(ui_forcedSwitch, LV_STATE_CHECKED);
	}
	g_syncing_forced_switch = false;
}

static void sync_bluetooth_switch_from_ble()
{
	if (ui_blueToothSwitch == NULL)
	{
		return;
	}

	const bool should_check = ble_server_enabled();
	const bool is_checked = lv_obj_has_state(ui_blueToothSwitch, LV_STATE_CHECKED);
	if (should_check == is_checked)
	{
		return;
	}

	g_syncing_bluetooth_switch = true;
	if (should_check)
	{
		lv_obj_add_state(ui_blueToothSwitch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(ui_blueToothSwitch, LV_STATE_CHECKED);
	}
	g_syncing_bluetooth_switch = false;
}

static void sync_beep_switch_from_beep()
{
	if (ui_beepSwitch == NULL)
	{
		return;
	}

	const bool should_check = beep_enabled();
	const bool is_checked = lv_obj_has_state(ui_beepSwitch, LV_STATE_CHECKED);
	if (should_check == is_checked)
	{
		return;
	}

	g_syncing_beep_switch = true;
	if (should_check)
	{
		lv_obj_add_state(ui_beepSwitch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(ui_beepSwitch, LV_STATE_CHECKED);
	}
	g_syncing_beep_switch = false;
}

static void on_pson_switch_changed(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || ui_PSONSwitch == NULL || g_syncing_pson_switch)
	{
		return;
	}

	const bool enabled = lv_obj_has_state(ui_PSONSwitch, LV_STATE_CHECKED);
	if (enabled && !power_ctrl_force_enabled())
	{
		g_syncing_pson_switch = true;
		lv_obj_clear_state(ui_PSONSwitch, LV_STATE_CHECKED);
		g_syncing_pson_switch = false;
		show_force_off_popup();
		return;
	}

	power_ctrl_set_software_enabled(enabled);
}

static void on_forced_switch_changed(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || ui_forcedSwitch == NULL || g_syncing_forced_switch)
	{
		return;
	}

	const bool forced_off = lv_obj_has_state(ui_forcedSwitch, LV_STATE_CHECKED);
	power_ctrl_set_force_enabled(!forced_off);
	sync_pson_switch_from_power_ctrl();
}

static void on_bluetooth_switch_changed(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || ui_blueToothSwitch == NULL || g_syncing_bluetooth_switch)
	{
		return;
	}

	const bool enabled = lv_obj_has_state(ui_blueToothSwitch, LV_STATE_CHECKED);
	ble_server_set_enabled(enabled);
}

static void on_beep_switch_changed(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || ui_beepSwitch == NULL || g_syncing_beep_switch)
	{
		return;
	}

	const bool enabled = lv_obj_has_state(ui_beepSwitch, LV_STATE_CHECKED);
	beep_set_enabled(enabled);
}

void ui_init()
{
	if (g_screen1_group == NULL)
	{
		g_screen1_group = lv_group_create();
	}

	if (g_screen2_group == NULL)
	{
		g_screen2_group = lv_group_create();
	}

	if (g_screen3_group == NULL)
	{
		g_screen3_group = lv_group_create();
	}

	if (g_screen4_group == NULL)
	{
		g_screen4_group = lv_group_create();
	}

	lv_disp_t *disp = lv_disp_get_default();
	if (disp != NULL)
	{
		lv_theme_t *theme = lv_theme_default_init(disp,
												  lv_palette_main(LV_PALETTE_BLUE),
												  lv_palette_main(LV_PALETTE_RED),
												  false,
												  LV_FONT_DEFAULT);
		lv_disp_set_theme(disp, theme);
	}

	ui_Screen1_screen_init();
	ui_Screen2_screen_init();
	ui_Screen3_screen_init();
	ui_Screen4_screen_init();
	update_screen2_rom_info();

	/* Screen1 control group: weather, power switch, setting. */
	add_obj_to_group(g_screen1_group, ui_PSONSwitch);
	add_obj_to_group(g_screen1_group, ui_setting);

	/* Screen2 control group: all setting menu entries + return. */
	add_obj_to_group(g_screen2_group, ui_wifiSetting);
	add_obj_to_group(g_screen2_group, ui_systemSetting);
	add_obj_to_group(g_screen2_group, ui_powerSetting);
	add_obj_to_group(g_screen2_group, ui_screenAdd);
	add_obj_to_group(g_screen2_group, ui_screenReduce);
	add_obj_to_group(g_screen2_group, ui_brightNess);
	add_obj_to_group(g_screen2_group, ui_return);

	/* Screen3 control group: wifi actions + return. */
	add_obj_to_group(g_screen3_group, ui_openWifi);
	add_obj_to_group(g_screen3_group, ui_cleanWifi);
	add_obj_to_group(g_screen3_group, ui_return2);

	/* Screen4 control group: force switch + return. */
	add_obj_to_group(g_screen4_group, ui_forcedSwitch);
	add_obj_to_group(g_screen4_group, ui_beepSwitch);
	add_obj_to_group(g_screen4_group, ui_blueToothSwitch);
	add_obj_to_group(g_screen4_group, ui_return3);

	if (ui_setting != NULL)
	{
		lv_obj_add_event_cb(ui_setting, on_setting_enter, LV_EVENT_CLICKED, NULL);
	}

	if (ui_return != NULL)
	{
		lv_obj_add_event_cb(ui_return, on_return_to_main, LV_EVENT_CLICKED, NULL);
	}

	if (ui_wifiSetting != NULL)
	{
		lv_obj_add_event_cb(ui_wifiSetting, on_wifi_setting_enter, LV_EVENT_CLICKED, NULL);
	}

	if (ui_systemSetting != NULL)
	{
		lv_obj_add_event_cb(ui_systemSetting, on_system_setting_enter, LV_EVENT_CLICKED, NULL);
	}

	if (ui_return2 != NULL)
	{
		lv_obj_add_event_cb(ui_return2, on_return_to_screen2, LV_EVENT_CLICKED, NULL);
	}

	if (ui_return3 != NULL)
	{
		lv_obj_add_event_cb(ui_return3, on_return3_to_screen2, LV_EVENT_CLICKED, NULL);
	}

	if (ui_openWifi != NULL)
	{
		lv_obj_add_event_cb(ui_openWifi, on_open_wifi, LV_EVENT_CLICKED, NULL);
	}

	if (ui_cleanWifi != NULL)
	{
		lv_obj_add_event_cb(ui_cleanWifi, on_clean_wifi, LV_EVENT_CLICKED, NULL);
	}

	if (ui_brightNess != NULL)
	{
		sync_brightness_slider_from_display();
		lv_obj_add_event_cb(ui_brightNess, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
	}

	if (ui_PSONSwitch != NULL)
	{
		sync_pson_switch_from_power_ctrl();
		lv_obj_add_event_cb(ui_PSONSwitch, on_pson_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);
	}

	if (ui_forcedSwitch != NULL)
	{
		sync_forced_switch_from_power_ctrl();
		lv_obj_add_event_cb(ui_forcedSwitch, on_forced_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);
	}

	if (ui_beepSwitch != NULL)
	{
		sync_beep_switch_from_beep();
		lv_obj_add_event_cb(ui_beepSwitch, on_beep_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);
	}

	if (ui_blueToothSwitch != NULL)
	{
		sync_bluetooth_switch_from_ble();
		lv_obj_add_event_cb(ui_blueToothSwitch, on_bluetooth_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);
	}

	if (ui_screenAdd != NULL)
	{
		lv_obj_add_event_cb(ui_screenAdd, on_rotate_add, LV_EVENT_CLICKED, NULL);
	}

	if (ui_screenReduce != NULL)
	{
		lv_obj_add_event_cb(ui_screenReduce, on_rotate_reduce, LV_EVENT_CLICKED, NULL);
	}

	// Hide warning icon initially (shown only when warning is active)
	if (ui_warning != NULL)
	{
		lv_obj_add_flag(ui_warning, LV_OBJ_FLAG_HIDDEN);
	}

	lv_disp_load_scr(ui_Screen1);
	set_active_group(g_screen1_group);
	lv_group_focus_obj(ui_PSONSwitch);
	
}

void ui_update_work_time(uint32_t seconds)
{
	if (ui_runTime == NULL)
	{
		return;
	}

	static bool time_base_ready = false;
	static uint32_t last_source_seconds = 0;
	static uint32_t run_seconds = 0;

	if (!time_base_ready)
	{
		last_source_seconds = seconds;
		time_base_ready = true;
	}

	uint32_t delta = 0;
	if (seconds >= last_source_seconds)
	{
		delta = seconds - last_source_seconds;
	}
	last_source_seconds = seconds;

	sync_pson_switch_from_power_ctrl();
	sync_forced_switch_from_power_ctrl();
	sync_beep_switch_from_beep();
	sync_bluetooth_switch_from_ble();
	const bool ps_on = power_ctrl_output_enabled();
	update_screen2_rom_info();
	if (lv_scr_act() == ui_Screen3)
	{
		update_screen3_wifi_info();
	}

	if (ui_Image1 != NULL)
	{
		static bool last_wifi_connected = false;
		static bool wifi_icon_initialized = false;
		const bool wifi_connected = wifi_portal_sta_connected();
		if (wifi_portal_sta_connected())
		{
			if (!wifi_icon_initialized || !last_wifi_connected)
			{
				lv_img_set_src(ui_Image1, &ui_img_808252844);
			}
		}
		else
		{
			if (!wifi_icon_initialized || last_wifi_connected)
			{
				lv_img_set_src(ui_Image1, &ui_img_546246141);
			}
		}
		last_wifi_connected = wifi_connected;
		wifi_icon_initialized = true;
	}

	if (ui_PSON != NULL)
	{
		static bool last_ps_on = false;
		static bool ps_label_initialized = false;
		if (ps_on)
		{
			if (!ps_label_initialized || !last_ps_on)
			{
				set_label_text_if_changed(ui_PSON, "PSON");
				lv_obj_set_style_text_color(ui_PSON, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
		}
		else
		{
			if (!ps_label_initialized || last_ps_on)
			{
				set_label_text_if_changed(ui_PSON, "PSON");
				lv_obj_set_style_text_color(ui_PSON, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
		}
		last_ps_on = ps_on;
		ps_label_initialized = true;
	}

	if (ps_on)
	{
		run_seconds += delta;
		lv_obj_clear_flag(ui_runTime, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_text_color(ui_runTime, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	}
	else
	{
		lv_obj_clear_flag(ui_runTime, LV_OBJ_FLAG_HIDDEN);
		const lv_color_t blink_color = ((seconds & 1U) != 0U)
										 ? lv_color_hex(0xFFFFFF)
										 : lv_color_hex(0x9A9A9A);
		lv_obj_set_style_text_color(ui_runTime, blink_color, LV_PART_MAIN | LV_STATE_DEFAULT);
	}

	const uint32_t h = run_seconds / 3600U;
	const uint32_t m = (run_seconds % 3600U) / 60U;
	const uint32_t s = run_seconds % 60U;

	char buf[24];
	lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u", static_cast<unsigned>(h), static_cast<unsigned>(m), static_cast<unsigned>(s));
	set_label_text_if_changed(ui_runTime, buf);
}

void ui_update_power_data(const pmbus_data_t *data)
{
	if (data == NULL)
	{
		return;
	}

	if (ui_inputVoltage == NULL || ui_inputCurrent == NULL || ui_inputPower == NULL ||
			ui_outputVoltage == NULL || ui_outputCurrent == NULL || ui_outputPower == NULL ||
			ui_tempBoard == NULL || ui_temp1 == NULL || ui_temp2 == NULL || ui_fanSpeed == NULL )
	{
		return;
	}
	char buf[32];

	lv_snprintf(buf, sizeof(buf), "%05.1fV", static_cast<double>(data->voltage_in_v));
	lv_label_set_text(ui_inputVoltage, buf);

	if (data->current_in_a < 0.25f)
	{
		lv_snprintf(buf, sizeof(buf), "<0.25A");
	}
	else
	{
		lv_snprintf(buf, sizeof(buf), "%05.2fA", static_cast<double>(data->current_in_a));
	}
	lv_label_set_text(ui_inputCurrent, buf);

	if (data->power_in_w < 20.0f)
	{
		lv_snprintf(buf, sizeof(buf), "<20W");
	}
	else
	{
		lv_snprintf(buf, sizeof(buf), "%05.1fW", static_cast<double>(data->power_in_w));
	}
	lv_label_set_text(ui_inputPower, buf);

	lv_snprintf(buf, sizeof(buf), "%05.2fV", static_cast<double>(data->voltage_out_v));
	lv_label_set_text(ui_outputVoltage, buf);

	if (data->current_out_a < 3.0f)
	{
		lv_snprintf(buf, sizeof(buf), "<3A");
	}
	else
	{
		lv_snprintf(buf, sizeof(buf), "%05.2fA", static_cast<double>(data->current_out_a));
	}
	lv_label_set_text(ui_outputCurrent, buf);

	if (data->power_out_w < 36.0f)
	{
		lv_snprintf(buf, sizeof(buf), "<36W");
	}
	else
	{
		lv_snprintf(buf, sizeof(buf), "%05.1fW", static_cast<double>(data->power_out_w));
	}
	lv_label_set_text(ui_outputPower, buf);

	lv_snprintf(buf, sizeof(buf), "效率:%05.2f%%", static_cast<double>(data->efficiency_percent));
	lv_label_set_text(ui_effi, buf);

	lv_snprintf(buf, sizeof(buf), "板温:%05.2f", static_cast<double>(g_board_temp_c));
	lv_label_set_text(ui_tempBoard, buf);

	lv_snprintf(buf, sizeof(buf), "温度1:%05.2f", static_cast<double>(data->temp1_c));
	lv_label_set_text(ui_temp1, buf);

	lv_snprintf(buf, sizeof(buf), "温度2:%05.2f", static_cast<double>(data->temp2_c));
	lv_label_set_text(ui_temp2, buf);

	lv_snprintf(buf, sizeof(buf), "风速:%.0fRpm", static_cast<double>(data->fan_speed_rpm));
	lv_label_set_text(ui_fanSpeed, buf);


}

void ui_set_board_temp(float temp_c)
{
	g_board_temp_c = temp_c;
}

lv_group_t *ui_get_input_group()
{
	return g_active_group;
}
