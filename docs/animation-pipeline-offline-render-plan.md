# Animation Pipeline Offline Render Plan

This is a separate future plan for host-side animation rendering and analysis.
It is not part of the current embedded animation pipeline refactor.

The goal is to reproduce GPU animation output with enough fidelity that visual
bugs can be analyzed as frame data instead of by live LED inspection.

## Goals

- run animation scripts on the host without ESP32 hardware
- emit deterministic binary traces for raw layer buffers, final HSV frames, and
  converted RGB frames
- compare renderer backends, blend modes, timing, and dithering assumptions
- support automated checks for per-pixel hue/value discontinuities between
  frames
- provide a small Python viewer for owner-side playback and inspection

## Fidelity Direction

The host path should not become a simplified mock that diverges from firmware.
A likely direction is a parallel host animation engine that shares the
animation, primitive, compositor, and topology code where practical, and draws
through selected FastLED sources or a faithful local equivalent where the
firmware path depends on FastLED behavior.

Exact fidelity questions to resolve before implementation:

- whether to compile FastLED conversion code into the host tool directly
- how to represent temporal dithering and frame cadence
- how to encode physical LED topology and GPU ownership
- whether traces store every intermediate layer or only selected probes
- how scripts should express PubSub commands, wheel updates, FFT frames, and
  timing

## Initial Trace Needs

The first useful trace format should include:

- topology/version header
- frame index and timestamp
- active command/update script identifier
- optional per-layer HSV frames
- final HSV frame
- final RGB frame

## First Validation Case

Reproduce two immediately adjacent center waves:

- green wave
- purple wave
- no intentional gap

The analysis should report isolated white/pink flashes, hue ownership
ping-pong, and value discontinuities per physical pixel.
