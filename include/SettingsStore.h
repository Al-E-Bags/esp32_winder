#pragma once
#include <Arduino.h>
#include <Preferences.h>

//
//  Winder settings + Nextion helpers (header-only)
//  Drop-in replacement for SettingsStore.h
//

namespace Winder
{

  // -------------------------
  // Settings model
  // -------------------------
  struct Settings
  {
    uint32_t baseTurns;     // profile’s base turns (e.g. 8000)
    uint16_t overwindTurns; // user extra turns (UI: nOverwind)
    uint16_t maxRPM;        // UI: nRPM
    uint16_t accel;         // UI: nAccel
    bool dirCW;             // motor direction
    uint8_t lastPage;       // optional (remember UI page)
    uint8_t profileId;      // <-- add this
  };

  // -------------------------
  // Robust NVS-backed store (per-field keys)
  // - tolerant to struct changes (no raw blob, no CRC required)
  // - bump kVersion if you add/change fields
  // -------------------------
  class SettingsStore
  {
  public:
    SettingsStore() = default;

    bool begin()
    {
      // Always open RW so saves actually write
      return prefs.begin("winder", /*readOnly=*/false);
    }

    // Defaults you want at first boot / reset-to-defaults
    // Defaults you want at first boot / reset-to-defaults
    void setDefaultsInRam()
    {
      s.baseTurns = 8000;
      s.overwindTurns = 100; // (SAFE: 50..200)
      s.maxRPM = 600;        // (SAFE: 300..1200)
      s.accel = 800;         // CHANGED to fit 0..1000 (was 1200)
      s.dirCW = true;
      s.lastPage = 0;
      s.profileId = 0;
    }

    // Returns true if settings are usable (seeds defaults on first run / version change)
    // Returns true if settings are usable (seeds defaults on first run / version change)
    bool load()
    {
      // Always seed RAM with your canonical defaults first
      setDefaultsInRam();

      const uint16_t ver = prefs.getUShort("ver", 0);
      if (ver != kVersion)
      {
        // Persist the new schema defaults and we're done
        return save();
      }

      // Overlay stored values on top of defaults
      s.baseTurns = prefs.getUInt("baseTurns", s.baseTurns);
      s.overwindTurns = prefs.getUShort("overwindTurns", s.overwindTurns);
      s.maxRPM = prefs.getUShort("maxRPM", s.maxRPM);
      s.accel = prefs.getUShort("accel", s.accel);
      s.dirCW = prefs.getBool("dirCW", s.dirCW);
      s.lastPage = prefs.getUChar("lastPage", s.lastPage);
      s.profileId = prefs.getUChar("profileId", s.profileId);
      return true;
    }

    // Persist current RAM copy to NVS
    bool save()
    {
      clampToSafeRange_(); // sanitize before persisting

      size_t ok = 0;
      ok += prefs.putUShort("ver", kVersion) > 0;
      ok += prefs.putUInt("baseTurns", s.baseTurns) > 0;
      ok += prefs.putUShort("overwindTurns", s.overwindTurns) > 0;
      ok += prefs.putUShort("maxRPM", s.maxRPM) > 0;
      ok += prefs.putUShort("accel", s.accel) > 0;
      ok += prefs.putBool("dirCW", s.dirCW) > 0;
      ok += prefs.putUChar("lastPage", s.lastPage) > 0;
      ok += prefs.putUChar("profileId", s.profileId) > 0;

      return ok >= 8; // all fields written
    }

    // Reset RAM to defaults and persist
    bool restoreDefaults()
    {
      setDefaultsInRam();
      clampToSafeRange_(); // keep defaults within the safe ranges
      return save();
    }

    // Access
    Settings &edit() { return s; }
    const Settings &get() const { return s; }

  private:
    static constexpr uint16_t kVersion = 2; // bump when you add/change fields
    Preferences prefs;
    Settings s{};

    static constexpr int RPM_MIN = 300, RPM_MAX = 1200;
    static constexpr int ACC_MIN = 0, ACC_MAX = 1000;
    static constexpr int OW_MIN = 50, OW_MAX = 200;

    static inline int clampi_(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi
                                                                                       : v; }

    void clampToSafeRange_()
    {
      s.maxRPM = clampi_(s.maxRPM, RPM_MIN, RPM_MAX);
      s.accel = clampi_(s.accel, ACC_MIN, ACC_MAX);
      s.overwindTurns = clampi_(s.overwindTurns, OW_MIN, OW_MAX);
      // baseTurns/dirCW/lastPage/profileId don’t need clamping
    }
  };

  // -------------------------
  // Nextion helpers (optional but handy)
  // - keep these light; they’re harmless if a target doesn’t exist
  // -------------------------
  namespace NX
  {

    // Color palette (RGB565)
    // tweak to your theme if desired
    static constexpr uint16_t COL_TEXT = 65535; // white
    static constexpr uint16_t COL_BG = 0;       // black
    static constexpr uint16_t COL_OK = 2016;    // green
    static constexpr uint16_t COL_BAD = 63488;  // red

    // Output UART for HMI
    static HardwareSerial *out = nullptr;

    inline void setOut(HardwareSerial *s) { out = s; }

    inline void term()
    {
      if (!out)
        return;
      out->write(0xFF);
      out->write(0xFF);
      out->write(0xFF);
    }

    // Send raw command (RAM String)
    inline void cmd(const String &s)
    {
      if (!out)
        return;
      out->print(s);
      term();
    }

    // Send raw command (Flash string)
    inline void cmd(const __FlashStringHelper *fs)
    {
      if (!out)
        return;
      out->print(fs);
      term();
    }

    // Build "obj.val=123"
    inline void setVal(const __FlashStringHelper *obj, uint32_t v)
    {
      if (!out)
        return;
      String s((const __FlashStringHelper *)obj);
      s += F(".val=");
      s += v;
      cmd(s);
    }

    // Build "obj.txt="...""
    inline void setTxt(const __FlashStringHelper *obj, const __FlashStringHelper *txt)
    {
      if (!out)
        return;
      String s((const __FlashStringHelper *)obj);
      s += F(".txt=");
      s += '\"';
      s += String(txt);
      s += '\"';
      cmd(s);
    }

    // Build "obj.txt=" + RAM string
    inline void setTxt(const __FlashStringHelper *obj, const String &txt)
    {
      if (!out)
        return;
      String s((const __FlashStringHelper *)obj);
      s += F(".txt=");
      s += '\"';
      s += txt;
      s += '\"';
      cmd(s);
    }

    // Build "obj.bco=COLOR"
    inline void setBco(const __FlashStringHelper *obj, uint32_t color)
    {
      if (!out)
        return;
      String s((const __FlashStringHelper *)obj);
      s += F(".bco=");
      s += color;
      cmd(s);
    }

    // Build "obj.pco=COLOR"
    inline void setPco(const __FlashStringHelper *obj, uint32_t color)
    {
      if (!out)
        return;
      String s((const __FlashStringHelper *)obj);
      s += F(".pco=");
      s += color;
      cmd(s);
    }

    // Show/hide object
    inline void vis(const __FlashStringHelper *obj, bool on)
    {
      if (!out)
        return;
      String s(F("vis "));
      s += String(obj);
      s += F(",");
      s += (on ? F("1") : F("0"));
      cmd(s);
    }

    // Quick validity tint (pco/bco + ref); harmless if object absent
    inline void markValid(const __FlashStringHelper *name, bool ok)
    {
      Winder::NX::setPco(name, Winder::NX::COL_TEXT); // always readable text
      Winder::NX::setBco(name, ok ? Winder::NX::COL_BG : Winder::NX::COL_BAD);
      // optional: Winder::NX::ref(name);
    }

    // Start button enable/disable (optional; ignore if you don’t have bStart)
    inline void setStartEnabled(bool on)
    {
      // If you have a dedicated style, tweak these two lines to your actual object
      // They’re harmless if "bStart" doesn’t exist (with bkcmd=0)
      String s1(F("bStart.bco="));
      s1 += (on ? COL_OK : COL_BAD);
      cmd(s1);
      String s2(F("bStart.en="));
      s2 += (on ? F("1") : F("0"));
      cmd(s2);
    }
    inline void toastBoth(const __FlashStringHelper *msg, uint32_t color)
    {
      // page0
      setTxt(F("page0.tToast"), String(msg));
      setBco(F("page0.tToast"), color);
      vis(F("page0.tToast"), true);
      cmd(F("page0.tmrToast.en=1"));
      // page1
      setTxt(F("page1.tToast"), String(msg));
      setBco(F("page1.tToast"), color);
      vis(F("page1.tToast"), true);
      cmd(F("page1.tmrToast.en=1"));
    }

  } // namespace NX

} // namespace Winder
