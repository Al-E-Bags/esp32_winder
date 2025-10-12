# Winder (PlatformIO)

## Environments
- `esp32s3` — ESP32-S3-DevKitC-1, Arduino framework
- `esp32dev` — Classic ESP32-DevKitC

## Build / Upload / Monitor
- Select the environment in the VS Code PlatformIO status bar.
- Build:  PlatformIO: Build
- Upload: PlatformIO: Upload
- Monitor: PlatformIO: Monitor (115200 baud)

## Next steps
- Confirm baseline compiles and uploads.
- Add `#include "SettingsStore.h"` into `src/main.cpp` when ready.
- We’ll then integrate NVS + Nextion helpers incrementally, testing after each tiny step.
