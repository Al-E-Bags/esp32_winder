#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace Winder {

// ---------- App settings & versions ----------
static constexpr uint16_t SETTINGS_VERSION = 3;   // bump when struct layout changes
static constexpr const char* NVS_NAMESPACE = "winder";

// Safer name to avoid clashes with any 'struct Profile' elsewhere.
enum class PickupProfile : uint8_t { Strat=0, Tele=1, P90=2, Jazzmaster=3, Humbucker=4 };

// Persisted settings (stable order)
struct Settings {
  // identity / integrity
  uint16_t version;
  uint16_t size;       // sizeof(Settings) when saved
  uint32_t crc32;

  // Winding config
  PickupProfile profile;      // pickup type
  uint32_t      baseTurns;    // e.g. 8000
  int32_t       bridgeOffsetTurns; // e.g. +200 for hotter bridge (can be negative)
  float         overwindPct;  // e.g. 0..30 (%)

  // Motion
  uint16_t maxRPM;
  uint16_t accel;             // your units
  bool     dirCW;

  // Cal/IO
  float    vinCal;            // calibration scalar
  float    rpmScale;          // rpm feedback scalar

  // UI state (optional)
  uint8_t  lastPage;
};

// ---------- Defaults ----------
inline Settings makeDefaults() {
  Settings s{};
  s.version = SETTINGS_VERSION;
  s.size    = sizeof(Settings);
  s.crc32   = 0;

  s.profile = PickupProfile::Strat;
  s.baseTurns = 8000;
  s.bridgeOffsetTurns = 0;
  s.overwindPct = 0.0f;

  s.maxRPM = 1200;
  s.accel  = 600;
  s.dirCW  = true;

  s.vinCal = 1.0f;
  s.rpmScale = 1.0f;

  s.lastPage = 0;
  return s;
}

// ---------- CRC32 (tiny, fast) ----------
inline uint32_t crc32_update_(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t i=0;i<8;i++) crc = (crc>>1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
  return crc;
}
inline uint32_t crc32_calc_(const void* data, size_t len) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i=0;i<len;i++) crc = crc32_update_(crc, p[i]);
  return ~crc;
}

// ---------- Store wrapper (header-only) ----------
class SettingsStore {
public:
  SettingsStore() : _prefs(), _settings(makeDefaults()) {}

  bool begin() { return _prefs.begin(NVS_NAMESPACE, /*readOnly=*/false); }

  const Settings& get() const { return _settings; }
  Settings&       edit()      { return _settings; }

  // Load with validation (version, size, CRC). Returns true if valid/migrated payload applied.
  bool load() {
    if (!_prefs.isKey("blob")) { _settings = makeDefaults(); return false; }

    size_t sz = _prefs.getBytesLength("blob");
    if (sz != sizeof(Settings)) { _settings = makeDefaults(); return false; }

    Settings tmp{};
    _prefs.getBytes("blob", &tmp, sizeof(Settings));

    if (tmp.version != SETTINGS_VERSION || tmp.size != sizeof(Settings)) {
      _settings = migrate_(tmp);   // graceful forward migration
      save();                      // write back in new format
      return true;
    }

    uint32_t expect = tmp.crc32;
    tmp.crc32 = 0;
    if (crc32_calc_(&tmp, sizeof(Settings)) != expect) {
      _settings = makeDefaults();
      return false;
    }

    _settings = tmp;
    return true;
  }

  // Save
  bool save() {
    Settings tmp = _settings;
    tmp.version = SETTINGS_VERSION;
    tmp.size    = sizeof(Settings);
    tmp.crc32   = 0;
    tmp.crc32   = crc32_calc_(&tmp, sizeof(Settings));
    size_t w = _prefs.putBytes("blob", &tmp, sizeof(Settings));
    return (w == sizeof(Settings));
  }

  // Restore factory defaults
  bool restoreDefaults() {
    _settings = makeDefaults();
    return save();
  }

private:
  // Basic migration stub (extend if you add older layouts later)
  static Settings migrate_(const Settings& /*oldAny*/) {
    return makeDefaults();
  }

  Preferences _prefs;
  Settings    _settings;
};

// ---------- Nextion helpers (serial-agnostic, no F()+String concat) ----------
namespace NX {

static Stream* out = &Serial;                 // default to Serial until you setOut(&HMI)
inline void setOut(Stream* s) { out = s; }

static constexpr uint16_t COL_OK    = 2016;
static constexpr uint16_t COL_WARN  = 64512;
static constexpr uint16_t COL_BAD   = 63488;
static constexpr uint16_t COL_MUTED = 33808;
static constexpr uint16_t COL_FG    = 65535;
static constexpr uint16_t COL_BG    = 0;

inline void sendTerminator() { out->write(0xFF); out->write(0xFF); out->write(0xFF); }

inline void ref(const String& obj) {
  out->print(F("ref ")); out->print(obj); sendTerminator();
}
inline void cmd(const char* s) { out->print(s); sendTerminator(); }
inline void cmd(const String& s){ out->print(s); sendTerminator(); }

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
inline void setPco(const String& obj, uint16_t color) {
  out->print(obj); out->print(F(".pco=")); out->print(color); sendTerminator();
}
inline void setBco(const String& obj, uint16_t color) {
  out->print(obj); out->print(F(".bco=")); out->print(color); sendTerminator();
}
inline void vis(const String& obj, bool on) {
  out->print(F("vis ")); out->print(obj); out->print(','); out->print(on ? 1 : 0); sendTerminator();
}
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




// #pragma once
// #include <Arduino.h>
// #include <Preferences.h>

// // ---------- Nextion helpers (serial-agnostic, no F()+String concat) ----------
// //namespace NX {
// namespace Winder {

// struct Settings {            // <- present
//   uint16_t version, size;
//   uint32_t crc32;
//   // ... (other fields incl. baseTurns, accel, dirCW)
//   uint32_t baseTurns;
//   uint16_t accel;
//   bool     dirCW;
//   // ...
// };

// class SettingsStore {        // <- present
// public:
//   SettingsStore();
//   bool begin();
//   bool load();
//   bool save();
//   bool restoreDefaults();
//   const Settings& get() const;
//   Settings&       edit();
// private:
//   // ...
// };

// static Stream* out = &Serial2;                 // default
// inline void setOut(Stream* s) { out = s; }     // call this to use your HMI serial

// static constexpr uint16_t COL_OK    = 2016;
// static constexpr uint16_t COL_WARN  = 64512;
// static constexpr uint16_t COL_BAD   = 63488;
// static constexpr uint16_t COL_MUTED = 33808;
// static constexpr uint16_t COL_FG    = 65535;
// static constexpr uint16_t COL_BG    = 0;

// inline void sendTerminator() { out->write(0xFF); out->write(0xFF); out->write(0xFF); }

// inline void ref(const String& obj) { out->print(F("ref ")); out->print(obj); sendTerminator(); }
// inline void cmd(const char* s)     { out->print(s); sendTerminator(); }
// inline void cmd(const String& s)   { out->print(s); sendTerminator(); }

// inline void setTxt(const String& obj, const String& txt) {
//   String safe = txt; safe.replace("\"","\\\"");
//   out->print(obj); out->print(F(".txt=\"")); out->print(safe); out->print('\"'); sendTerminator();
// }
// inline void setVal(const String& obj, int32_t v) {
//   out->print(obj); out->print(F(".val=")); out->print(v); sendTerminator();
// }
// inline void setFloat(const String& obj, float f, uint8_t dp=1) {
//   char buf[32]; dtostrf(f,0,dp,buf);
//   out->print(obj); out->print(F(".txt=\"")); out->print(buf); out->print('\"'); sendTerminator();
// }
// inline void setPco(const String& obj, uint16_t color) { out->print(obj); out->print(F(".pco=")); out->print(color); sendTerminator(); }
// inline void setBco(const String& obj, uint16_t color) { out->print(obj); out->print(F(".bco=")); out->print(color); sendTerminator(); }
// inline void vis(const String& obj, bool on) { out->print(F("vis ")); out->print(obj); out->print(','); out->print(on ? 1 : 0); sendTerminator(); }

// inline void pulseOk(const String& obj) { setBco(obj, COL_OK); ref(obj); }
// inline void markValid(const String& obj, bool ok) {
//   setPco(obj, ok ? COL_FG : COL_BAD);
//   setBco(obj, ok ? COL_BG : (COL_BAD - 256));
//   ref(obj);
// }
// inline void showSavedToast() {
//   setTxt(F("tToast"), F("Saved"));
//   setBco(F("gToast"), COL_OK);
//   vis(F("gToast"), true);
//   out->print(F("tmrToast.en=1")); sendTerminator();
// }
// inline void showErrorToast(const String& message) {
//   setTxt(F("tToast"), message);
//   setBco(F("gToast"), COL_BAD);
//   vis(F("gToast"), true);
//   out->print(F("tmrToast.en=1")); sendTerminator();
// }
// inline void setStartEnabled(bool en) {
//   out->print(F("bStart.en=")); out->print(en ? 1 : 0); sendTerminator();
//   setPco(F("bStart"), en ? COL_FG : COL_MUTED);
//   setBco(F("bStart"), en ? COL_OK : COL_MUTED);
// }

// } // namespace NX


