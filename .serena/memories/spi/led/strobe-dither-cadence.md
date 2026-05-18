# LED Strobe Dither Cadence

FastLED 3.10.3 disables controller dithering when its internal measured FPS is below 100, and it starts with `m_nFPS = 0`, so early `show()` calls can disable dithering before a valid FPS measurement exists.

The LED stack now uses a master GPIO present strobe at 125 Hz rather than exact 100 Hz. GPU reset monitoring after upload on 2026-05-17 showed both GPU nodes logging `FastLED temporal dithering enabled at measured FPS=125` after startup.

The FastLED backend tracks `FastLED.getFPS()`: it waits for a measured FPS >= 100, re-enables `BINARY_DITHER` once, and if FPS later drops below 100 after a valid cadence was observed, it logs an error-level state change and marks dithering for re-enable on recovery. This avoids calling `setDither()` every frame while preserving visibility into cadence failures.