# Amp 1 Tone Stack Handoff

Last updated: 2026-06-12

Branch: `feature/amp1-tone-stack`

## Amp 1 model context

- Character A: Zoom Tri Metal pedal direct into an ENGL Savage 120 power amp.
- Character B: a fairly high-gain ENGL Fireball 60.
- Both are more distorted than the Amp 2 models and are intended to provide a different high-gain/lead character while remaining versatile.

## Design discussion

The previous generic tone stack was not a good match:

- Mid was centered too low.
- Treble and Presence were both high shelves and overlapped too much.
- The extreme boost/cut ranges were usually not useful.

The agreed first step was a lead-focused post-model tone stack that preserves the existing seven-knob UI and keeps noon flat. A possible later step is to replace Depth with a pre-model `TIGHT` control, but that should only be considered after evaluating this version.

## Implemented first-pass voicing

- Bass: `130 Hz` low shelf, `-7 / +4 dB`.
- Mid: cut at `600 Hz` down to `-6 dB`; boost at `900 Hz` up to `+5 dB`.
- Treble: `2.5 kHz` peaking filter, `-5 / +4 dB`.
- Presence: `6.2 kHz` high shelf, `-7 / +2.5 dB`.
- Depth: `85 Hz` resonance, `-4 / +5 dB`.
- All controls are neutral at `5`.

Amp 1 now uses a dedicated `Amp1ToneStack`. Parameter IDs, defaults, UI, master behavior, and serialization are unchanged. Amp 2 and Amp 3 behavior are unchanged.

## Files changed

- `NeuralAmpModeler/ToneStack.h`
- `NeuralAmpModeler/ToneStack.cpp`
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `NeuralAmpModeler/NeuralAmpModeler.cpp`

Code commit: `daf9a5d Add lead-focused Amp 1 tone stack`

## Evaluation

Build manually with `Release | x64` and run standalone without the debugger (`Ctrl+F5`).

Test both Amp 1 characters with all tone controls at noon, then sweep one control at a time. Check that:

- Treble changes pick attack and upper-mid articulation.
- Presence controls fizz and openness without duplicating Treble.
- Mid cut removes congestion while Mid boost adds lead projection.
- Bass and Depth remain useful throughout their ranges.
- Extreme settings are musical rather than destructive.

No build or audio validation had been run when this handoff was written.

## Possible follow-up

After evaluating this version, consider replacing Depth with a pre-model `TIGHT` control. That control would reduce low frequencies before the NAM model so it changes distortion response, palm-mute tightness, and clarity rather than only applying post-model EQ.

Any follow-up must remain deterministic and allocation/lock/I/O free in the audio callback.
