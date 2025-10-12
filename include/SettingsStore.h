#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ---------- Nextion helpers (serial-agnostic, no F()+String concat) ----------
namespace NX {

static Stream* out = &Serial2;                 // default
inline void setOut(Stream* s) { out = s; }     // call this to use your HMI serial

static constexpr uint16_t COL_OK    = 2016;
static constexpr uint16_t COL_WARN  = 64512;
static constexpr uint16_t COL_BAD   = 63488;
static constexpr uint16_t COL_MUTED = 33808;
static constexpr uint16_t COL_FG    = 65535;
static constexpr uint16_t COL_BG    = 0;

inline void sendTerminator() { out->write(0xFF); out->write(0xFF); out->write(0xFF); }

inline void ref(const String& obj) { out->print(F("ref ")); out->print(obj); sendTerminator(); }
inline void cmd(const char* s)     { out->print(s); sendTerminator(); }
inline void cmd(const String& s)   { out->print(s); sendTerminator(); }

inline void setTxt(const String& obj, const String& txt) {
  String safe = txt; safe.replace("\"","\\\"");
  out->print(obj); out->print(F(".txt=\"")); out->print(safe); out->print('\"'); sendTerminator();
}
inline void setVal(const String& obj, int32_t v) {
  out->print(obj); out->print(F(".val=")); out->print(v); sendTerminator();
}
inline void setFloat(const String& obj, float f, uint8_t dp=1) {
  char buf[32]; dtostrf(f,0,dp,buf);
  out->print(obj); out->print(F(".txt=\"")); out->print(buf); out->print('\"'); sendTerminator();
}
inline void setPco(const String& obj, uint16_t color) { out->print(obj); out->print(F(".pco=")); out->print(color); sendTerminator(); }
inline void setBco(const String& obj, uint16_t color) { out->print(obj); out->print(F(".bco=")); out->print(color); sendTerminator(); }
inline void vis(const String& obj, bool on) { out->print(F("vis ")); out->print(obj); out->print(','); out->print(on ? 1 : 0); sendTerminator(); }

inline void pulseOk(const String& obj) { setBco(obj, COL_OK); ref(obj); }
inline void markValid(const String& obj, bool ok) {
  setPco(obj, ok ? COL_FG : COL_BAD);
  setBco(obj, ok ? COL_BG : (COL_BAD - 256));
  ref(obj);
}
inline void showSavedToast() {
  setTxt(F("tToast"), F("Saved"));
  setBco(F("gToast"), COL_OK);
  vis(F("gToast"), true);
  out->print(F("tmrToast.en=1")); sendTerminator();
}
inline void showErrorToast(const String& message) {
  setTxt(F("tToast"), message);
  setBco(F("gToast"), COL_BAD);
  vis(F("gToast"), true);
  out->print(F("tmrToast.en=1")); sendTerminator();
}
inline void setStartEnabled(bool en) {
  out->print(F("bStart.en=")); out->print(en ? 1 : 0); sendTerminator();
  setPco(F("bStart"), en ? COL_FG : COL_MUTED);
  setBco(F("bStart"), en ? COL_OK : COL_MUTED);
}

} // namespace NX



} // namespace Winder
