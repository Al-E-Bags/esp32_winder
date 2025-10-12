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

// Your Nextion port (keep this name; we’ll route helpers to it)
HardwareSerial HMI(2);

// Global NVS store
static Winder::SettingsStore store;

// (Optional) helper: send the Nextion 0xFF terminator three times
static inline void nxTerm()
{
  HMI.write(0xFF);
  HMI.write(0xFF);
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
  Winder::NX::setVal(F("nBase"), s.baseTurns);
  Winder::NX::setVal(F("nAccel"), s.accel);
  Winder::NX::setVal(F("nRPM"), s.maxRPM);
  Winder::NX::setTxt(F("tDir"), s.dirCW ? F("CW") : F("CCW"));

  // If you have a profile dropdown:
  Winder::NX::cmd(String(F("ddProfile.val=")) + String((uint8_t)s.profile));
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
  HMI.begin(115200);

  // Point the Nextion helper output at your HMI serial
  Winder::NX::setOut(&HMI);

  // --- NVS init + load sanity ---
  if (!store.begin())
  {
    Serial.println(F("NVS: begin() failed (using defaults in RAM)"));
  }
  else
  {
    bool ok = store.load();
    Serial.println(ok ? F("NVS: load OK") : F("NVS: no saved blob -> using defaults"));
    const auto &s = store.get();
    printSettings(s); // <— new line (nice one-liner)
    pushSettingsToHMI(s);
    // Optional visual: enable Start button look
    Winder::NX::setStartEnabled(true);
  }

  // Optional: make Nextion a bit chatty (uncomment if you want)
  // nx("bkcmd=3");

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
    // Example Nextion keep-alive:
    // nx("tStatus.txt=\"HB %lu\"", (unsigned long)now);
  }

  // Echo any HMI replies to the console (handy while wiring things up)
  while (HMI.available())
  {
    int c = HMI.read();
    Serial.write(c);
  }

  // --- USB Serial commands: b(+100 baseTurns), s(save), r(factory reset) ---
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
        // Preferred: toast overlay (requires gToast/tToast + tmrToast on HMI)
        Winder::NX::showSavedToast();
        // Fallback status line (harmless if tStatus doesn't exist)
        Winder::NX::setTxt(F("tStatus"), F("Saved ✓"));
        // Optional: flash a Save button if present
        // Winder::NX::pulseOk(F("bSave"));
      }
      else
      {
        Winder::NX::showErrorToast(F("Save failed"));
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
        Winder::NX::showSavedToast();
        Winder::NX::setTxt(F("tStatus"), F("Defaults restored"));
      }
      else
      {
        Winder::NX::showErrorToast(F("Restore failed"));
        Winder::NX::setTxt(F("tStatus"), F("Restore failed ✗"));
      }
    }
    // (ignore other keys)
  }
}
