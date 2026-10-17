# Windows release checks

The implementation and Linux validation do not establish Windows DAW behavior.
Before a production release:

1. Require a green `Windows VST3` workflow for the exact release commit.
   Retain its compiler output and CTest artifacts.
2. Run a current VST3 validator / pluginval against the Windows bundle, including
   mono/stereo layouts and float/double processing. The local C++ tests validate
   the processor directly, not every host wrapper path.
3. Install the complete `.vst3` bundle on Windows 11. Scan in Ableton Live;
   insert on mono and stereo tracks; reopen a saved project containing several
   instances with different presets, custom Extended controls and oversampling.
4. Exercise automation, host bypass, quality changes, offline export,
   sample-rate changes, very small/large buffers and editor scaling at multiple
   Windows DPI settings. Confirm no dropout or unexpected latency shift.
5. Measure multiple-instance real-time CPU on the target audio interface and
   buffer size. Cloud wall-clock benchmarks are not DAW dropout guarantees.
6. Perform controlled listening on drums, bass, vocals, full mixes, sustained
   tones and transients. Check low-level coloration and extreme drive for
   unwanted noise, DC, harshness or clicks.
7. If claiming similarity, compare licensed reference renders at matched input
   levels and settings with `Tools/analyze.py --reference ... --candidate ...`. Fit documented independent
   calibration constants and repeat the entire regression suite. An unmeasured
   approximation is not an exact hardware or proprietary DSP model.
8. Review JUCE/SDK license obligations, version the release and retain artifacts.

No licensed original render, Ableton installation or Windows audio device is
available inside the Linux cloud machine. These checks must be reported as
unrun until performed on the corresponding platform.
