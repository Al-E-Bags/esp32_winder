/*
  Winder Master – S3 motion + Nextion + RS-485 (Nodes = PG only)
  Update:
    - Status colors on HMI (OK=green, warn=yellow, off=gray, fault=red)
    - Humbucker profile added
    - VIN Check broadcasts PG? and shows "Off" if no node replies (rotary pos #1)
    - 1s background auto-poll keeps status current
    - MICROSTEP = 4 for quiet motion (match your TB6600 DIP)
  Pins:
    - Nextion HMI UART2: RX=13, TX=14 (autobauds to 115200)
    - RS-485 UART1: RX=47, TX=21 (auto-direction transceiver)
    - TB6600: STEP=9, DIR=10, EN=11
*/

#include <Arduino.h>
#include "SettingsStore.h" // header-only; no .cpp needed

// ----------------------------------------------------------------------------
// Minimal, compile-now baseline using your existing HardwareSerial HMI(2)
// ----------------------------------------------------------------------------

// Validation flags (start false so Start is gray until fields are valid)
static bool v_overwind = false;
static bool v_rpm = false;
static bool v_accel = false;

// Tweak these to your machine’s safe limits
static constexpr int32_t RPM_MIN = 300;
static constexpr int32_t RPM_MAX = 1000;
static constexpr int32_t ACC_MIN = 50;
static constexpr int32_t ACC_MAX = 1200;

static constexpr int32_t OVER_MIN = 0;
static constexpr int32_t OVER_MAX = 150;

static inline bool inRangeInt(int32_t v, int32_t lo, int32_t hi)
{
  return v >= lo && v <= hi;
}
static inline bool allValid()
{
  return v_overwind && v_rpm && v_accel;
}

// Your Nextion port (keep this name; we’ll route helpers to it)
HardwareSerial HMI(2);

// Global NVS store
static Winder::SettingsStore store;

// (Optional) helper: send the Nextion 0xFF terminator three times
static inline void nxTerm()
{
  for (int i = 0; i < 3; i++)
    HMI.write(0xFF);
}

// (Optional) helper: formatted Nextion command print
static void nx(const char *fmt, ...)
{
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  HMI.print(buf);
  nxTerm();
}

// Simple heartbeat
static uint32_t lastBeat = 0;

static void printSettings(const Winder::Settings &s)
{
  Serial.printf("Settings: baseTurns=%u, accel=%u, dirCW=%s, lastPage=%u\n",
                (unsigned)s.baseTurns, (unsigned)s.accel,
                s.dirCW ? "true" : "false", (unsigned)s.lastPage);
}

static void pushSettingsToHMI(const Winder::Settings &s)
{
  // If you have these objects in HMI, this will update them.
  // If not, harmless no-ops on Nextion (it just ignores unknown objects).
  Winder::NX::setVal(F("nBaseTurns"), s.baseTurns);
  Winder::NX::setVal(F("nAccel"), s.accel);
  Winder::NX::setVal(F("nRPM"), s.maxRPM);
  Winder::NX::setTxt(F("tDir"), s.dirCW ? F("CW") : F("CCW"));
  Winder::NX::cmd(String(F("cb0Profile.val=")) + s.profileId);

  // If you have a profile dropdown (cb0Profile):
  Winder::NX::setVal(F("cb0Profile"), s.profileId);
}

static String nxBuf;
static uint8_t nxFF = 0;
static String pendingSetKey; // remembers "SET key" waiting for its value

static long parseIntSafe(const String &s)
{
  String d;
  d.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i)
  {
    char c = s[i];
    if ((c >= '0' && c <= '9') || (c == '-' && d.length() == 0))
      d += c;
  }
  return d.length() ? d.toInt() : 0;
}

// Update the store's RAM copy from a "SET key value"
static void applyKV(const String &key, long val)
{
  Winder::Settings &s = store.edit(); // <-- write into the store

  if (key.equalsIgnoreCase("nRPM"))
  {
    s.maxRPM = (int)val;
  }
  else if (key.equalsIgnoreCase("nAccel"))
  {
    s.accel = (int)val;
  }
  else if (key.equalsIgnoreCase("nOverwind"))
  {
    s.overwindTurns = (int)val;
  }
  else if (key.equalsIgnoreCase("baseTurns"))
  {
    s.baseTurns = (int)val;
  }
  else if (key.equalsIgnoreCase("profileId"))
  {
    s.profileId = (uint8_t)val;
  }
  else if (key.equalsIgnoreCase("lastPage"))
  {
    s.lastPage = (uint8_t)val;
  }
  // add other keys here as needed
}

static void refreshValidationAndUIFromSettings()
{
  const auto &s = store.get();

  // Re-validate against current limits
  v_overwind = inRangeInt((int32_t)s.overwindTurns, OVER_MIN, OVER_MAX);
  v_rpm = inRangeInt((int32_t)s.maxRPM, RPM_MIN, RPM_MAX);
  v_accel = inRangeInt((int32_t)s.accel, ACC_MIN, ACC_MAX);

  // Push values to CURRENT page
  Winder::NX::setVal(F("nOverwind"), s.overwindTurns);
  Winder::NX::setVal(F("nRPM"), s.maxRPM);
  Winder::NX::setVal(F("nAccel"), s.accel);

  // Color feedback
  Winder::NX::markValid(F("nOverwind"), v_overwind);
  Winder::NX::markValid(F("nRPM"), v_rpm);
  Winder::NX::markValid(F("nAccel"), v_accel);

  // Start button state
  Winder::NX::setStartEnabled(allValid());
}

// Push current settings to page0 (+ optional page1) regardless of active page
static void repaintAllPagesFromSettings()
{
  const auto &s = store.get();

  // write values (page0 targets)
  Winder::NX::cmd(String(F("page0.nOverwind.val=")) + s.overwindTurns);
  Winder::NX::cmd(String(F("page0.nRPM.val=")) + s.maxRPM);
  Winder::NX::cmd(String(F("page0.nAccel.val=")) + s.accel);

  // validate against your ranges
  bool ok_over = inRangeInt((int32_t)s.overwindTurns, OVER_MIN, OVER_MAX);
  bool ok_rpm = inRangeInt((int32_t)s.maxRPM, RPM_MIN, RPM_MAX);
  bool ok_acc = inRangeInt((int32_t)s.accel, ACC_MIN, ACC_MAX);

  // page-qualified markValid so it works on any page
  Winder::NX::markValid(F("page0.nOverwind"), ok_over);
  Winder::NX::markValid(F("page0.nRPM"), ok_rpm);
  Winder::NX::markValid(F("page0.nAccel"), ok_acc);

  Winder::NX::setStartEnabled(ok_over && ok_rpm && ok_acc);
}

// ---------- full parser ----------
static void handleNextionMessage(const String &msg)
{
  String m = msg;
  m.trim();

  // 2) value-only message for a prior "SET key"
  if (pendingSetKey.length())
  {
    long val = parseIntSafe(m);
    String key = pendingSetKey;
    pendingSetKey = "";
    applyKV(key, val);
    return;
  }

  // 3) fresh "SET key" or "SET key value"
  if (m.startsWith("SET "))
  {
    int sp1 = m.indexOf(' ', 4);
    if (sp1 < 0)
      return; // malformed

    String key = m.substring(4, sp1); // e.g., "nBase"
    String rest = m.substring(sp1 + 1);
    rest.trim();

    if (rest.length() == 0)
    { // two-part mode
      pendingSetKey = key;
      return;
    }

    long val = parseIntSafe(rest); // one-line mode
    applyKV(key, val);
  }

  // SAVE
  if (m == "CMD SAVE" || m == "SAVE")
  {
    bool ok = store.save();
    if (ok)
      repaintAllPagesFromSettings();
    Winder::NX::toastBoth(ok ? F("Saved") : F("Save failed"),
                          ok ? Winder::NX::COL_OK : Winder::NX::COL_BAD);
    return;
  }

  // RESET
  if (m == "CMD RESET" || m == "RESET")
  {
    bool ok = store.restoreDefaults();
    store.load();                  // belt & braces
    repaintAllPagesFromSettings(); // update page0 immediately
    Winder::NX::toastBoth(ok ? F("Defaults restored") : F("Reset failed"),
                          ok ? Winder::NX::COL_OK : Winder::NX::COL_BAD);
    return;
  }
}

// Read Nextion bytes, detect 0xFF 0xFF 0xFF terminator, then dispatch
static void pollHMI()
{
  while (HMI.available())
  {
    int b = HMI.read();

    if (b == 0xFF)
    {
      if (++nxFF >= 3)
      {
        if (nxBuf.length() > 0)
        {
          Serial.print(F("[NX] "));
          Serial.println(nxBuf);
          handleNextionMessage(nxBuf); // your existing parser
        }
        nxBuf = "";
        nxFF = 0;
      }
    }
    else
    {
      nxFF = 0;
      nxBuf += char(b);
    }
  }
}

void setup()
{
  // USB serial console
  Serial.begin(115200);
  while (!Serial)
  {
    delay(10);
  } // harmless for UART-based boards
  Serial.println();
  Serial.println(F("Winder (PIO clean baseline + NVS) booting..."));

  // Nextion / HMI serial (adjust pins if needed)
  // If you wired custom pins, use: HMI.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  HMI.begin(115200, SERIAL_8N1, /*RX=*/13, /*TX=*/14);
  // Point the Nextion helper output at your HMI serial
  Winder::NX::setOut(&HMI);
  Winder::NX::vis(F("tToast"), false);

  Winder::NX::cmd("bkcmd=3");

  Winder::NX::setStartEnabled(false); // show Start as disabled/gray at boot
  // (Optional) also tint the nBase field as “needs attention” on boot:
  Winder::NX::markValid(F("nBase"), false);

  // --- NVS init + load sanity ---
  if (!store.begin())
  {
    Serial.println(F("NVS: begin() failed (using defaults in RAM)"));
  }
  else
  {
    bool ok = store.load();
    // refreshValidationAndUIFromSettings();
    repaintAllPagesFromSettings();
    Serial.println(ok ? F("NVS: load OK") : F("NVS: no saved blob -> using defaults"));
    const auto &s = store.get();
    printSettings(s); // <— new line (nice one-liner)
    pushSettingsToHMI(s);
    // Optional visual: enable Start button look
    Winder::NX::setStartEnabled(true);
  }

  Winder::NX::markValid(F("nBase"), false);
  Winder::NX::markValid(F("nRPM"), false);
  Winder::NX::markValid(F("nAccel"), false);
  Winder::NX::setStartEnabled(false);

  Serial.println(F("Setup complete."));
}

void loop()
{
  static uint32_t lastBeat = 0;
  const uint32_t now = millis();

  // Heartbeat every 1s
  if (now - lastBeat >= 1000)
  {
    lastBeat = now;
    Serial.printf("[HB] %lu ms\n", (unsigned long)now);
  }

  // Read Nextion bytes, detect 0xFF 0xFF 0xFF terminator, parse "print ..." strings
  while (HMI.available())
  {
    int b = HMI.read();
    Serial.write(b); // echo raw for debugging

    if (b == 0xFF)
    {
      if (++nxFF >= 3)
      {
        if (nxBuf.length() > 0)
        {
          Serial.print(F("[NX] "));
          Serial.println(nxBuf);
          handleNextionMessage(nxBuf);
        }
        nxBuf = "";
        nxFF = 0;
      }
    }
    else
    {
      nxFF = 0;
      nxBuf += (char)b;
    }
  }
  // --- USB Serial commands: b(+100 baseTurns), s(save), r(factory reset), t(toast test) ---
  while (Serial.available())
  {
    int c = Serial.read();

    if (c == 'b' || c == 'B')
    {
      auto &s = store.edit();
      s.baseTurns += 100;
      Serial.println(F("Bumped baseTurns by +100"));
      printSettings(s);
      pushSettingsToHMI(s);
    }

    else if (c == 's' || c == 'S')
    {
      bool ok = store.save();
      Serial.println(ok ? F("Saved to NVS") : F("SAVE FAILED"));
      if (ok)
      {
        // minimal toast
        Winder::NX::setTxt(F("tToast"), F("Saved"));
        Winder::NX::setBco(F("tToast"), Winder::NX::COL_OK);
        Winder::NX::vis(F("tToast"), true);
        Winder::NX::cmd(F("tmrToast.en=1"));
        Winder::NX::setTxt(F("tStatus"), F("Saved ✓"));
      }
      else
      {
        Winder::NX::setTxt(F("tToast"), F("Save failed"));
        Winder::NX::setBco(F("tToast"), Winder::NX::COL_BAD);
        Winder::NX::vis(F("tToast"), true);
        Winder::NX::cmd(F("tmrToast.en=1"));
        Winder::NX::setTxt(F("tStatus"), F("Save failed ✗"));
      }
    }

    else if (c == 'r' || c == 'R')
    {

      bool ok = store.restoreDefaults();
      Serial.println(ok ? F("Restored defaults + saved") : F("RESTORE FAILED"));

      const auto &s2 = store.get();
      printSettings(s2);
      pushSettingsToHMI(s2);

      if (ok)
      {
        Winder::NX::setTxt(F("tToast"), F("Defaults restored"));
        Winder::NX::setBco(F("tToast"), Winder::NX::COL_OK);
        Winder::NX::vis(F("tToast"), true);
        Winder::NX::cmd(F("tmrToast.en=1"));
        Winder::NX::setTxt(F("tStatus"), F("Defaults restored"));
      }
      else
      {
        Winder::NX::setTxt(F("tToast"), F("Restore failed"));
        Winder::NX::setBco(F("tToast"), Winder::NX::COL_BAD);
        Winder::NX::vis(F("tToast"), true);
        Winder::NX::cmd(F("tmrToast.en=1"));
        Winder::NX::setTxt(F("tStatus"), F("Restore failed ✗"));
      }
    }
  }
}
