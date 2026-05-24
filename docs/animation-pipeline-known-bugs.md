# Animation Pipeline Known Bugs

This document tracks known rendering and visual defects separately from the
animation pipeline migration plan. Some issues may be caused by the pipeline,
FastLED conversion or dithering, timing, power, or physical LED behavior. Do
not close a bug by visual guesswork.

Codex cannot directly inspect live LED output. After the animation pipeline is
structurally complete, add local rendering tooling that can write animation
frames to a binary format. That format should be usable by automated analysis
and by a simple Python viewer for owner-side visual inspection.

## AP-001: Closely Spaced Wave Hue Flicker

Status: open

Reported behavior:

- Start one wave in green.
- Immediately follow it with another wave in purple.
- Some LEDs between the waves flicker between white and pink.
- The artifact appears only in individual cases, but it is visually irritating
  and headache-inducing.

Current hypotheses:

- ambiguous same-buffer overlap in the current single-frame render path
- unstable hue ownership in `MaxValue` or `AddValue` blend ties
- HSV-to-RGB conversion behavior in overlapping low-value pixels
- FastLED temporal dithering making an existing render artifact more visible
- FastLED output timing or hardware behavior

No root cause is established. Implementing layers, scratch rendering, opacity,
and alpha blending may remove or change the symptom, but that should be treated
as a hypothesis until frame-level tooling can prove where the artifact appears.

Required future tooling:

- deterministic command script for the two-wave reproduction
- local renderer that writes raw HSV layer buffers, final HSV frames, and
  converted RGB frames to a binary trace
- analysis that detects per-pixel hue/value discontinuities between frames
- option to compare renderer backends and dithering settings
- Python viewer that can play the binary trace for owner inspection

Validation questions:

- Does the artifact exist in final HSV frames before FastLED conversion?
- Does it first appear after HSV-to-RGB conversion?
- Does it depend on temporal dithering?
- Does it disappear with scratch-to-layer composition and stable blend tie
  rules?
- Does it reproduce only on physical LEDs?

