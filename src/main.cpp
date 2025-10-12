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

static void printSettings(const Winder::Settings& s) {
  Serial.printf("Settings: baseTurns=%u, accel=%u, dirCW=%s, lastPage=%u\n",
                (unsigned)s.baseTurns, (unsigned)s.accel,
                s.dirCW ? "true" : "false", (unsigned)s.lastPage);
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
  if (!store.begin()) {
    Serial.println(F("NVS: begin() failed (using defaults in RAM)"));
  } else {
    bool ok = store.load();
    Serial.println(ok ? F("NVS: load OK") : F("NVS: no saved blob -> using defaults"));
    const auto& s = store.get();
    printSettings(s);   // <— new line (nice one-liner)
  }



  // Optional: make Nextion a bit chatty (uncomment if you want)
  // nx("bkcmd=3");

  Serial.println(F("Setup complete."));
}

void loop()
{
  const uint32_t now = millis();

  // Heartbeat every 1s
  if (now - lastBeat >= 1000)
  {
    lastBeat = now;
    Serial.printf("[HB] %lu ms\n", (unsigned long)now);
    // Example Nextion keep-alive (comment in if desired):
    // nx("tStatus.txt=\"HB %lu\"", (unsigned long)now);
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
    }
    else if (c == 's' || c == 'S')
    {
      bool ok = store.save();
      Serial.println(ok ? F("Saved to NVS") : F("SAVE FAILED"));
    }
    else if (c == 'r' || c == 'R')
    {
      bool ok = store.restoreDefaults();
      Serial.println(ok ? F("Restored defaults + saved") : F("RESTORE FAILED"));
      printSettings(store.get());
    }
  }

  // Echo any HMI replies to the console (handy while wiring things up)
  while (HMI.available())
  {
    int c = HMI.read();
    Serial.write(c);
  }
}

// #include <Arduino.h>
// #include <stdarg.h>
// #include "SettingsStore.h"

// static Winder::SettingsStore store;

// // ---------- Profiles ----------

// enum ProfileId : uint8_t
// {
//   PROF_STRAT_NECK = 0,
//   PROF_STRAT_MIDDLE,
//   PROF_STRAT_BRIDGE,
//   PROF_STRAT_BRIDGE_PLUS,
//   PROF_TELE_NECK,
//   PROF_TELE_BRIDGE,
//   PROF_P90_NECK,
//   PROF_P90_BRIDGE,
//   PROF_JAZZMASTER_NECK,
//   PROF_JAZZMASTER_BRIDGE,
//   PROF_HUMBUCKER_NECK,
//   PROF_HUMBUCKER_BRIDGE, // NEW
//   PROF_COUNT
// };

// struct Profile
// {
//   const char *name;
//   uint32_t baseTurns;
//   uint16_t rpm;
//   uint16_t accel;
//   bool cw;
// };
// int32_t g_bridgeExtraTurns = 0;

// static const Profile PROFILES[PROF_COUNT] = {
//     {"Strat Neck", 7800, 300, 800, true},
//     {"Strat Middle", 7800, 300, 800, false},
//     {"Strat Bridge +", 8000, 320, 900, true},
//     {"Tele Bridge", 8200, 300, 800, true},
//     {"P90", 8500, 280, 700, true},
//     {"Jazzmaster", 8200, 300, 800, true},
//     {"Humbucker", 5000, 300, 800, true}, // per-bobbin default
// };

// static inline uint32_t effectiveTurns(ProfileId id)
// {
//   uint32_t base = PROFILES[id].baseTurns;
//   if (id == PROF_STRAT_BRIDGE_PLUS)
//   {
//     int64_t t = (int64_t)base + (int64_t)g_bridgeExtraTurns;
//     if (t < 0)
//       t = 0;
//     return (uint32_t)t;
//   }
//   return base;
// }

// // ---------- Pins / UARTs ----------
// #define HMI_RX 13
// #define HMI_TX 14
// HardwareSerial HMI(2);

// #define PIN_M_RX 47
// #define PIN_M_TX 21
// #define PIN_M_REDE -1
// #define STEP_PULSE_US 4 // 3–8 µs works well with TB6600

// #define RS485_BAUD 115200
// HardwareSerial BUS(1);

// #define PIN_STEP 9
// #define PIN_DIR 10
// #define PIN_EN 11
// #define EN_ACTIVE LOW

// // ---------- Motion ----------
// #define STEPS_REV 200 // set 400 if your motor is 0.9°
// #define MICROSTEP 4   // your selected microstep (match TB6600 DIP)
// #define PULSES_REV ((uint32_t)STEPS_REV * (uint32_t)MICROSTEP)

// #define UI_IDLE_PERIOD_MS 100 // 10 Hz when idle
// #define UI_RUN_PERIOD_MS 500  // 2 Hz while motor is moving

// uint32_t g_uiNextMs = 0;
// uint32_t g_uiLastTurns = 0xFFFFFFFF;
// uint16_t g_uiLastPermille = 0xFFFF;
// uint16_t g_uiLastRPM = 0xFFFF;
// bool g_uiLastDir = true;

// enum RunState : uint8_t
// {
//   IDLE,
//   WINDING,
//   JOGGING,
//   PAUSED,
//   SOFT_STOP,
//   ABORTED
// };
// RunState g_state = IDLE;

// bool g_dirCW = true;
// uint32_t g_targetTurns = 0;
// uint32_t g_targetRPM = 300; // gentle default
// uint16_t g_accel = 800;     // gentle default
// uint32_t g_donePulses = 0;

// // Smooth ramp (pulses/sec)
// uint32_t g_targetPps = 0;
// uint32_t g_currentPps = 0;
// uint32_t g_accelPps = 2000; // pps per second (slew rate)

// uint32_t g_stepIntervalUs = 0;
// uint32_t g_lastStepUs = 0;

// bool g_lastVinOK = false; // updated on PG replies; blocks Start if false
// uint32_t g_lastVinMs = 0; // timestamp of last PG reply

// bool g_profileSelected = false; // only true after user picks a profile via HMI

// static inline uint32_t rpmToPps(uint32_t rpm)
// {
//   return (uint32_t)(((uint64_t)rpm * (uint32_t)PULSES_REV) / 60ULL);
// }

// // ---------- HMI helpers ----------
// static inline void nxTerm()
// {
//   HMI.write(0xFF);
//   HMI.write(0xFF);
//   HMI.write(0xFF);
// }

// void nx(const char *fmt, ...)
// {
//   char buf[128];
//   va_list ap;
//   va_start(ap, fmt);
//   vsnprintf(buf, sizeof(buf), fmt, ap);
//   va_end(ap);
//   HMI.print(buf);
//   nxTerm();
// }

// void nxSetTurns(uint32_t t)
// {
//   nx("tTurns.txt=\"%lu\"", (unsigned long)t);
// }
// void nxSetRPM(uint16_t rpm)
// {
//   nx("tRPM.txt=\"%u\"", (unsigned)rpm);
// }
// void nxSetDir(bool cw)
// {
//   nx("tDir.txt=\"%s\"", cw ? "CW" : "CCW");
// }
// void nxSetStatus(const char *s)
// {
//   nx("tStatus.txt=\"%s\"", s);
// }
// void nxSetProgPermille(uint16_t p)
// {
//   if (p > 1000)
//     p = 1000;
//   nx("jProgress.val=%u", (unsigned)p);
// }

// // Nextion 565 colors
// #define NX_COL_BLACK 0
// #define NX_COL_WHITE 65535
// #define NX_COL_RED 63488
// #define NX_COL_GREEN 2016
// #define NX_COL_BLUE 31
// #define NX_COL_YELLOW 65504
// #define NX_COL_GRAY 33808

// void nxSetStatusColor(const char *s, uint16_t color)
// {
//   nx("tStatus.bco=%u", (unsigned)color);
//   nxSetStatus(s);
// }
// inline void nxStatusOK(const char *s)
// {
//   nxSetStatusColor(s, NX_COL_GREEN);
// }
// inline void nxStatusWarn(const char *s)
// {
//   nxSetStatusColor(s, NX_COL_YELLOW);
// }
// inline void nxStatusErr(const char *s)
// {
//   nxSetStatusColor(s, NX_COL_RED);
// }
// inline void nxStatusOff()
// {
//   nxSetStatusColor("Off", NX_COL_GRAY);
// }

// void hmiAutobaud()
// {
//   HMI.begin(9600, SERIAL_8N1, HMI_RX, HMI_TX);
//   delay(50);
//   HMI.print("bauds=115200");
//   nxTerm();
//   delay(50);
//   HMI.print("bkcmd=0");
//   nxTerm();
//   delay(20);
//   HMI.end();
//   HMI.begin(115200, SERIAL_8N1, HMI_RX, HMI_TX);
// }

// // ---------- RS-485 helpers ----------
// void setupRs485()
// {
//   BUS.begin(RS485_BAUD, SERIAL_8N1, PIN_M_RX, PIN_M_TX);
// #if (PIN_M_REDE >= 0)
//   pinMode(PIN_M_REDE, OUTPUT);
//   digitalWrite(PIN_M_REDE, LOW);
// #endif
// }

// void busSendRaw(const char *s)
// {
// #if (PIN_M_REDE >= 0)
//   digitalWrite(PIN_M_REDE, HIGH);
//   delayMicroseconds(2);
// #endif
//   BUS.print(s);
//   BUS.write('\n');
// #if (PIN_M_REDE >= 0)
//   BUS.flush();
//   delayMicroseconds(2);
//   digitalWrite(PIN_M_REDE, LOW);
// #endif
// }

// void busSendTo(uint8_t node, const char *fmt, ...)
// {
//   char msg[128];
//   va_list ap;
//   va_start(ap, fmt);
//   vsnprintf(msg, sizeof(msg), fmt, ap);
//   va_end(ap);
//   char frame[160];
//   snprintf(frame, sizeof(frame), "@%u:%s", (unsigned)node, msg);
//   busSendRaw(frame);
// }

// // ---------- Direction helper (DIR setup time) ----------

// void setDir(bool cw)
// {
//   g_dirCW = cw;
//   digitalWrite(PIN_DIR, cw ? HIGH : LOW);
//   delayMicroseconds(10); // TB6600 DIR setup before first step
// }

// // ---------- Profiles / UI actions ----------
// volatile ProfileId g_activeProfile = PROF_STRAT_NECK;
// uint8_t g_activeNode = 1;

// void applyProfile(ProfileId id)
// {
//   g_activeProfile = id;
//   const Profile &P = PROFILES[id];

//   g_targetTurns = effectiveTurns(id);
//   g_targetRPM = P.rpm;
//   g_accel = P.accel;

//   setDir(P.cw);

//   g_targetPps = rpmToPps(g_targetRPM);
//   if (g_currentPps == 0)
//     g_currentPps = min<uint32_t>(g_targetPps, 500);
//   g_stepIntervalUs = (g_currentPps ? (1000000UL / g_currentPps) : 1000000UL);

//   nxSetStatus("ready");
// }

// void selectProfileId(uint8_t pid)
// {
//   g_activeNode = (pid == 0) ? 1 : (pid == 1) ? 2
//                                              : 3; // your mapping; fine
//   applyProfile((ProfileId)pid);
//   g_profileSelected = true; // <-- mark as chosen
// }

// void jog(bool cw)
// {
//   setDir(cw);
//   g_state = JOGGING;
//   g_targetPps = rpmToPps(g_targetRPM);
//   if (g_currentPps == 0)
//     g_currentPps = min<uint32_t>(g_targetPps, 500);
//   g_stepIntervalUs = (g_currentPps ? (1000000UL / g_currentPps) : 1000000UL);
//   nxSetStatus(cw ? "jog cw" : "jog ccw");
// }

// void pauseWinder()
// {
//   if (g_state == WINDING || g_state == JOGGING)
//   {
//     g_state = PAUSED;
//     nxSetStatus("paused");
//   }
// }

// void resumeWinder()
// {
//   if (g_state == PAUSED)
//   {
//     g_state = WINDING;
//     nxSetStatus("winding");
//   }
// }

// void softStop()
// {
//   g_state = SOFT_STOP;
//   nxSetStatus("soft stop");
// }
// void abortWinder()
// {
//   g_state = ABORTED;
//   nxSetStatus("aborted");
// }

// // Start a winding run to the profile’s target turns
// void startWinding()
// {
//   if (!g_profileSelected)
//   {
//     nxStatusErr("select profile");
//     return;
//   }

//   if (g_targetTurns == 0)
//   {
//     nxStatusErr("turns=0");
//     return;
//   }

//   if (!g_lastVinOK)
//   {
//     nxStatusErr("No PG / Off");
//     return;
//   }

//   digitalWrite(PIN_EN, EN_ACTIVE);
//   g_donePulses = 0;
//   g_targetPps = rpmToPps(g_targetRPM);
//   if (g_targetPps < 10)
//     g_targetPps = 10;
//   if (g_currentPps == 0)
//     g_currentPps = min<uint32_t>(g_targetPps, 500);
//   if (g_currentPps < 10)
//     g_currentPps = 10;
//   g_stepIntervalUs = 1000000UL / g_currentPps;
//   g_state = WINDING;
//   nxSetStatus("winding");
// }

// // Immediate HARD stop: halt motion right now
// void hardStopNow()
// {
//   g_state = IDLE;
//   g_targetPps = 0;
//   g_currentPps = 0;

//   // Stop pulse output and drop torque briefly
//   digitalWrite(PIN_STEP, LOW);
//   digitalWrite(PIN_EN, !EN_ACTIVE); // disable TB6600 momentarily
//   delay(5);
//   digitalWrite(PIN_EN, EN_ACTIVE); // re-enable (idle state)

//   nxSetStatus("hard stop");
// }

// // ---------- VIN Check / Auto detect ----------

// bool g_waitingVin = false;
// bool g_vinGot = false;
// uint32_t g_vinDeadlineMs = 0;

// void requestVinCheckRoundRobin()
// {
//   busSendRaw("PG?");
//   g_waitingVin = true;
//   g_vinGot = false;
//   g_vinDeadlineMs = millis() + 300;

//   // UI churn here causes audible ticks; only show when not moving
//   if (g_state == IDLE || g_state == PAUSED)
//   {
//     nxStatusWarn("vin check…");
//   }
// }

// // ---------- Parsers ----------

// String hmiLine, busLine;

// void handleHmiCmd(const String &s)
// {
//   // 1) ComboBox index-based selection (MUST be first)
//   if (s.startsWith("UI:PROFILE:IDX:"))
//   {
//     int idx = s.substring(16).toInt();
//     if (idx >= 0 && idx < PROF_COUNT)
//       selectProfileId((uint8_t)idx);
//     else
//       nxStatusErr("profile idx OOR");
//     return;
//   }

//   // 2) Legacy named tokens (map to your 12-profile enum)
//   if (s == "UI:PROFILE:NECK")
//   {
//     selectProfileId(PROF_STRAT_NECK);
//     return;
//   }
//   else if (s == "UI:PROFILE:MIDDLE")
//   {
//     selectProfileId(PROF_STRAT_MIDDLE);
//     return;
//   }
//   else if (s == "UI:PROFILE:BRIDGE")
//   {
//     selectProfileId(PROF_STRAT_BRIDGE);
//     return;
//   }
//   else if (s == "UI:PROFILE:BRIDGE_PLUS")
//   {
//     selectProfileId(PROF_STRAT_BRIDGE_PLUS);
//     return;
//   }
//   else if (s == "UI:PROFILE:TELE_NECK")
//   {
//     selectProfileId(PROF_TELE_NECK);
//     return;
//   }
//   else if (s == "UI:PROFILE:TELE_BRIDGE")
//   {
//     selectProfileId(PROF_TELE_BRIDGE);
//     return;
//   }
//   else if (s == "UI:PROFILE:P90_NECK")
//   {
//     selectProfileId(PROF_P90_NECK);
//     return;
//   }
//   else if (s == "UI:PROFILE:P90_BRIDGE")
//   {
//     selectProfileId(PROF_P90_BRIDGE);
//     return;
//   }
//   else if (s == "UI:PROFILE:JAZZ_NECK")
//   {
//     selectProfileId(PROF_JAZZMASTER_NECK);
//     return;
//   }
//   else if (s == "UI:PROFILE:JAZZ_BRIDGE")
//   {
//     selectProfileId(PROF_JAZZMASTER_BRIDGE);
//     return;
//   }
//   else if (s == "UI:PROFILE:HUM_NECK")
//   {
//     selectProfileId(PROF_HUMBUCKER_NECK);
//     return;
//   }
//   else if (s == "UI:PROFILE:HUM_BRIDGE")
//   {
//     selectProfileId(PROF_HUMBUCKER_BRIDGE);
//     return;
//   }

//   // 3) Motion controls
//   else if (s == "UI:START")
//   {
//     startWinding();
//     return;
//   }
//   else if (s == "UI:SOFT_STOP")
//   {
//     softStop();
//     return;
//   }
//   else if (s == "UI:HARD_STOP" || s == "UI:STOP")
//   {
//     hardStopNow();
//     return;
//   }
//   else if (s == "UI:ABORT")
//   {
//     abortWinder();
//     return;
//   }

//   // 4) Jog (optional)
//   else if (s == "UI:JOG:CW")
//   {
//     jog(true);
//     return;
//   }
//   else if (s == "UI:JOG:CCW")
//   {
//     jog(false);
//     return;
//   }

//   // 5) VIN check
//   else if (s == "UI:VIN_CHECK")
//   {
//     requestVinCheckRoundRobin();
//     return;
//   }

//   nxStatusErr("cmd?");
// }

// void handleBusLine(const String &line)
// {
//   Serial.print("[BUS RX] ");
//   Serial.println(line);
//   if (!line.startsWith("@"))
//     return;
//   int colon = line.indexOf(':', 2);
//   if (colon < 0)
//     return;
//   uint8_t node = (uint8_t)line.substring(1, colon).toInt();
//   String payload = line.substring(colon + 1);

//   if (payload.startsWith("PG:"))
//   {
//     g_vinGot = true;
//     uint16_t mv = (uint16_t)payload.substring(3).toInt();
//     g_lastVinOK = (mv >= 4600);
//     g_lastVinMs = millis(); // <-- freshness timestamp

//     char buf[48];
//     snprintf(buf, sizeof(buf), "node%u PG=%umV", node, (unsigned)mv);
//     if (mv < 4600)
//       nxStatusErr("fault:PG_LOW");
//     else
//       nxStatusOK(buf);
//     return;
//   }

//   else if (payload.startsWith("STAT:"))
//   {
//     nxSetStatus(payload.substring(5).c_str());
//   }
//   else if (payload.startsWith("ERR:"))
//   {
//     nxStatusErr(payload.c_str());
//   }
// }

// // ---------- Setup / Loop ----------
// void hmiSetup()
// {
//   hmiAutobaud();
//   // Initial UI state
//   nxSetTurns(0);
//   nxSetRPM(g_targetRPM);
//   nxSetDir(true);
//   nxSetProgPermille(0);
//   nxSetStatusColor("idle", NX_COL_BLUE);
// }

// void setup()
// {
//   pinMode(PIN_STEP, OUTPUT);
//   pinMode(PIN_DIR, OUTPUT);
//   pinMode(PIN_EN, OUTPUT);
//   digitalWrite(PIN_EN, EN_ACTIVE);
//   digitalWrite(PIN_STEP, LOW);

//   Serial.begin(115200);
//   hmiSetup();
//   setupRs485();

//   applyProfile(PROF_STRAT_NECK);
//   Serial.println("Master ready (S3 motion) - TB6600 STEP=9 DIR=10 EN=11; RS485 RX=47 TX=21");
// }

// void loop()
// {
//   // HMI intake
//   while (HMI.available())
//   {
//     char c = HMI.read();
//     if (c == '\n' || c == '\r')
//     {
//       hmiLine.trim();
//       while (hmiLine.length() && (uint8_t)hmiLine[0] <= 0x20)
//         hmiLine.remove(0, 1);
//       if (hmiLine.startsWith("UI:"))
//         handleHmiCmd(hmiLine);
//       hmiLine = "";
//     }
//     else if (hmiLine.length() < 120)
//       hmiLine += c;
//   }

//   // RS-485 intake
//   while (BUS.available())
//   {
//     char c = BUS.read();
//     if (c == '\n' || c == '\r')
//     {
//       busLine.trim();
//       if (busLine.length())
//         handleBusLine(busLine);
//       busLine = "";
//     }
//     else if (busLine.length() < 180)
//       busLine += c;
//   }

//   // Acceleration ramp (~10 ms) with deadband to prevent PPS hunting
//   static uint32_t rampMs = 0;
//   if (millis() - rampMs >= 10)
//   {
//     rampMs = millis();
//     int32_t delta = (int32_t)g_targetPps - (int32_t)g_currentPps;
//     int32_t step = (int32_t)(g_accelPps / 100); // 10 ms slice
//     const int32_t dead = 10;                    // ~10 pps deadband

//     if (delta > dead)
//       g_currentPps += (uint32_t)min<int32_t>(delta, step);
//     else if (delta < -dead)
//       g_currentPps -= (uint32_t)min<int32_t>(-delta, step);
//     else
//       g_currentPps = g_targetPps; // snap to target inside deadband

//     if (g_currentPps < 10)
//       g_currentPps = 10;
//     g_stepIntervalUs = 1000000UL / g_currentPps;
//   }

//   // Graceful soft stop: ramp target to 0, then idle
//   if (g_state == SOFT_STOP)
//   {
//     g_targetPps = 0; // let the ramp pull us down
//     if (g_currentPps <= 20)
//     { // near-zero threshold
//       g_state = IDLE;
//       nxSetStatus("stopped");
//     }
//   }

//   // One-shot pulse generator (jog/wind/soft-stop)
//   if (g_state == JOGGING || g_state == WINDING || g_state == SOFT_STOP)
//   {
//     uint32_t now = micros();
//     if (now - g_lastStepUs >= g_stepIntervalUs)
//     {
//       g_lastStepUs = now;

//       // one clean step pulse (rising-edge action)
//       digitalWrite(PIN_STEP, HIGH);
//       delayMicroseconds(STEP_PULSE_US);
//       digitalWrite(PIN_STEP, LOW);

//       g_donePulses++;

//       if (g_state == WINDING)
//       {
//         uint32_t targetPulses = g_targetTurns * PULSES_REV;
//         if (g_donePulses >= targetPulses)
//         {
//           g_state = IDLE;
//           nxSetStatus("complete");
//         }
//       }
//     }
//   }

//   // UI telemetry (throttled to reduce audible load modulation)
//   uint32_t period = (g_state == WINDING || g_state == JOGGING || g_state == SOFT_STOP)
//                         ? UI_RUN_PERIOD_MS
//                         : UI_IDLE_PERIOD_MS;

//   if (millis() - g_uiNextMs >= period)
//   {
//     g_uiNextMs = millis();

//     // Only send if value changed (minimize serial writes)
//     uint32_t turns = g_donePulses / PULSES_REV;
//     if (turns != g_uiLastTurns)
//     {
//       g_uiLastTurns = turns;
//       nxSetTurns(turns);
//     }

//     // Progress (permille) – also throttled + change-only
//     uint32_t tp = (g_targetTurns == 0) ? 1 : g_targetTurns;
//     uint64_t num = (uint64_t)g_donePulses * 1000ULL;
//     uint64_t den = (uint64_t)PULSES_REV * (uint64_t)tp;
//     uint16_t pm = (uint16_t)((den ? (num / den) : 0ULL) > 1000ULL ? 1000ULL : (num / den));
//     if (pm != g_uiLastPermille)
//     {
//       g_uiLastPermille = pm;
//       nxSetProgPermille(pm);
//     }

//     // RPM & DIR: only when they’ve actually changed
//     if ((uint16_t)g_targetRPM != g_uiLastRPM)
//     {
//       g_uiLastRPM = (uint16_t)g_targetRPM;
//       nxSetRPM(g_uiLastRPM);
//     }
//     if (g_dirCW != g_uiLastDir)
//     {
//       g_uiLastDir = g_dirCW;
//       nxSetDir(g_dirCW);
//     }
//   }

//   // VIN timeout -> show "Off" and clear VIN OK
//   // On VIN timeout with no reply -> Off and block Start
//   if (g_waitingVin && (int32_t)(millis() - g_vinDeadlineMs) >= 0)
//   {
//     if (!g_vinGot)
//     {
//       nxStatusOff();
//       g_lastVinOK = false; // <-- keep this
//     }
//     g_waitingVin = false;
//   }

//   static uint32_t lastAutoPoll = 0;
//   bool moving = (g_state == WINDING || g_state == JOGGING);
//   if (!moving && !g_waitingVin && (millis() - lastAutoPoll >= 1000))
//   {
//     lastAutoPoll = millis();
//     requestVinCheckRoundRobin();
//   }
// }
