#include <beep.h>

#include <Preferences.h>

namespace
{
static constexpr uint8_t kBeepPwmChannel = 3;
static constexpr uint8_t kBeepPwmResolutionBits = 8;
static constexpr uint32_t kBeepDefaultFreqHz = 2000;
static constexpr const char *kPrefsNs = "beep_cfg";
static constexpr const char *kPrefsEnabledKey = "enabled";

uint32_t g_beep_frequency_hz = kBeepDefaultFreqHz;
uint8_t g_beep_duty = 0;
bool g_beep_enabled = true;

// melody state
static const beep_note_t *g_melody = nullptr;
static size_t g_melody_length = 0;
static size_t g_melody_index = 0;
static uint32_t g_note_start_ms = 0;
static bool g_melody_playing = false;

static constexpr beep_note_t kMelodyPowerOn[] = {
  {523, 80},   // C5
  {659, 80},   // E5
  {784, 80},   // G5
  {1047, 150}, // C6
};

static constexpr beep_note_t kMelodyPowerOff[] = {
  {784, 80},   // G5
  {659, 80},   // E5
  {523, 80},   // C5
  {392, 200},  // G4
};

void beep_apply()
{
  ledcSetup(kBeepPwmChannel, g_beep_frequency_hz, kBeepPwmResolutionBits);
  ledcWrite(kBeepPwmChannel, g_beep_duty);
}

void beep_store_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, false))
  {
    prefs.putBool(kPrefsEnabledKey, g_beep_enabled);
    prefs.end();
  }
}

void beep_load_enabled()
{
  Preferences prefs;
  if (prefs.begin(kPrefsNs, true))
  {
    g_beep_enabled = prefs.getBool(kPrefsEnabledKey, true);
    prefs.end();
  }
}

void start_melody(const beep_note_t *notes, size_t count)
{
  if (!g_beep_enabled || notes == nullptr || count == 0)
  {
    return;
  }

  g_melody = notes;
  g_melody_length = count;
  g_melody_index = 0;
  g_melody_playing = true;
  g_note_start_ms = millis();

  const beep_note_t &first = notes[0];
  if (first.frequency_hz > 0)
  {
    beep_start(first.frequency_hz, 64);
  }
  else
  {
    beep_stop();
  }
}
}

void beep_init()
{
  pinMode(kBeepPin, OUTPUT);
  digitalWrite(kBeepPin, LOW);
  ledcAttachPin(kBeepPin, kBeepPwmChannel);
  beep_load_enabled();
  beep_stop();
}

void beep_start(uint32_t frequency_hz, uint8_t duty)
{
  if (!g_beep_enabled)
  {
    return;
  }

  g_beep_frequency_hz = frequency_hz;
  g_beep_duty = duty;
  beep_apply();
}

void beep_stop()
{
  g_beep_duty = 0;
  beep_apply();
}

void beep_set_frequency(uint32_t frequency_hz)
{
  g_beep_frequency_hz = frequency_hz;
  beep_apply();
}

void beep_set_duty(uint8_t duty)
{
  if (!g_beep_enabled)
  {
    return;
  }

  g_beep_duty = duty;
  beep_apply();
}

void beep_set_enabled(bool enabled)
{
  if (g_beep_enabled == enabled)
  {
    return;
  }

  g_beep_enabled = enabled;
  if (!g_beep_enabled)
  {
    beep_stop();
    g_melody_playing = false;
  }
  beep_store_enabled();
}

bool beep_enabled()
{
  return g_beep_enabled;
}

void beep_play_power_on()
{
  start_melody(kMelodyPowerOn, sizeof(kMelodyPowerOn) / sizeof(kMelodyPowerOn[0]));
}

void beep_play_power_off()
{
  start_melody(kMelodyPowerOff, sizeof(kMelodyPowerOff) / sizeof(kMelodyPowerOff[0]));
}

static constexpr beep_note_t kMelodyShutdownAlert[] = {
  {800, 100},
  {0, 60},
  {800, 100},
  {0, 60},
  {800, 100},
  {0, 60},
  {600, 150},
  {0, 100},
  {600, 150},
  {0, 100},
  {400, 300},
};

void beep_play_shutdown_alert()
{
  start_melody(kMelodyShutdownAlert, sizeof(kMelodyShutdownAlert) / sizeof(kMelodyShutdownAlert[0]));
}

// ── Warning beep (continuous, managed by beep_task) ──
static bool g_warning_beep_active = false;
static uint32_t g_warning_beep_next_ms = 0;
static bool g_warning_beep_on = false;
static uint8_t g_warning_beep_phase = 0;  // 0=beep1, 1=silence, 2=beep2, 3=longer silence

void beep_set_warning_active(bool active)
{
  if (active == g_warning_beep_active)
  {
    return;
  }

  g_warning_beep_active = active;
  if (!active)
  {
    beep_stop();
    g_warning_beep_on = false;
    g_warning_beep_phase = 0;
  }
  else
  {
    g_warning_beep_next_ms = millis();
    g_warning_beep_phase = 0;
    g_warning_beep_on = false;
  }
}

static void beep_warning_tick()
{
  if (!g_warning_beep_active)
  {
    return;
  }

  // Don't interfere with a playing melody
  if (g_melody_playing)
  {
    return;
  }

  const uint32_t now_ms = millis();
  if (now_ms < g_warning_beep_next_ms)
  {
    return;
  }

  switch (g_warning_beep_phase)
  {
  case 0:  // short beep
    beep_start(1000, 64);
    g_warning_beep_next_ms = now_ms + 120;
    g_warning_beep_phase = 1;
    break;
  case 1:  // short silence
    beep_stop();
    g_warning_beep_next_ms = now_ms + 80;
    g_warning_beep_phase = 2;
    break;
  case 2:  // second short beep
    beep_start(1000, 64);
    g_warning_beep_next_ms = now_ms + 120;
    g_warning_beep_phase = 3;
    break;
  case 3:  // longer silence before repeat
  default:
    beep_stop();
    g_warning_beep_next_ms = now_ms + 700;
    g_warning_beep_phase = 0;
    break;
  }
}

void beep_task()
{
  // Warning beep runs independently of melodies
  beep_warning_tick();

  if (!g_melody_playing)
  {
    return;
  }

  const uint32_t now_ms = millis();
  const beep_note_t &current = g_melody[g_melody_index];

  if (now_ms - g_note_start_ms < current.duration_ms)
  {
    return;
  }

  g_melody_index++;
  if (g_melody_index >= g_melody_length)
  {
    beep_stop();
    g_melody_playing = false;
    g_melody = nullptr;
    return;
  }

  g_note_start_ms = now_ms;
  const beep_note_t &next = g_melody[g_melody_index];
  if (next.frequency_hz > 0)
  {
    beep_start(next.frequency_hz, 64);
  }
  else
  {
    beep_stop();
  }
}
