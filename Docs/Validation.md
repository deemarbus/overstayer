# Validation and calibration

The pure C++ wet engine and the JUCE host wrapper have separate tests. These tests establish implementation behavior; they do not establish a measured match to Softube or hardware. Licensed-reference audio has not been supplied.

## Reproduce the core measurements

```sh
cmake -S . -B build-dsp -DIVORY_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp --output-on-failure
./build-dsp/IvoryDspTests build-dsp/validation
python3 Tools/analyze.py --measurements build-dsp/validation --output build-dsp/validation/plots
```

The analysis step requires NumPy and Matplotlib and works offline. The executable itself requires only a C++20 standard library. On Visual Studio multi-configuration generators, run `build-dsp/Release/IvoryDspTests.exe` and add `--config Release` to the build command and `-C Release` to CTest.

`Tests/DspTests.cpp` exits nonzero when a behavioral check fails, continues through independent checks and writes CSV measurements plus original PCM24 WAV fixtures. The coherent sine uses 32,768 samples, bin 683 at 48 kHz (1000.48828125 Hz), after a 250 ms settling period. Harmonic magnitudes use direct coherent DFT coefficients through harmonic 20; THD excludes noise, DC and the fundamental. The reported values apply to the wet engine at **1x**. They do not measure oversampling alias rejection.

Required core checks cover increasing distortion versus drive, even/odd comparison with 2ND, second-harmonic increase and normalized spectral change with DUAL, all six nonlinear emphasis profiles, small-signal Extended compensation, independent PRE/POST bands, HP/LP attenuation and resonance, Extended EMPHASIS gating, common input switches in both views, INPUT/AUTO GAIN calibration, transient crest reduction, impulses, mono/stereo independence, all five requested sample rates in float and double, finite output during extreme automation, subnormal suppression and actual nonlinear activity. CPU figures are descriptive measurements on the executing machine, not hard pass/fail limits.

## Recorded core result

On 2026-10-17, Linux x64, GCC C++20 with `-O2 -Wall -Wextra -Wconversion`, all **12 test groups passed**. Representative measurements:

| Measurement | Result | Conditions |
|---|---:|---|
| THD, low input amplitude | 0.006882% | Peak input 0.02, 0 dB drive, emphasis off |
| THD, moderate amplitude | 3.18466% | Peak input 0.8, same settings |
| THD, high amplitude | 11.18736% | Peak input 1.8, same settings |
| Even/odd harmonic ratio | 0.06305 → 1.68105 | Normal → 2ND, peak input 0.8 |
| DUAL H2 relative to H1 | +10.133 dB change | DUAL versus single at peak input 0.8 |
| Minimum pairwise Classic waveform difference | 4.453% RMS-relative | Six profiles, +12 dB INPUT, multitone stimulus |
| Worst complementary-EQ relative response error | 1.30 × 10⁻⁹ dB | Double, +9 dB PRE on four bands, 100 Hz–18 kHz test points, peak input 10⁻⁵ |
| HP rejection | −24.100 dB | 125 Hz into 500 Hz HP, resonance minimum |
| LP rejection | −25.692 dB | 8 kHz into 2 kHz LP, resonance minimum |
| HP/LP maximum-resonance gain at cutoff | +2.277 / +2.279 dB | Q 1.3; resonant peak occurs away from exact cutoff |
| Extended lower section disabled residual | 0 | HP/LP and EQ bypassed through EMPHASIS |
| INPUT gain, AUTO GAIN off | 12.000 dB | +12 dB control, small-signal 1 kHz |
| INPUT gain, AUTO GAIN on | approximately 0 dB | Same stimulus |
| Transient crest | 20.937 → 9.288 dB | Repeated decaying tone bursts, +12 dB drive, DUAL/2ND |
| Impulse tail after 750 ms | 2.96 × 10⁻¹⁸ | DUAL/2ND enabled |
| Activity, quiet/driven | 0.00000354 / 0.30342 | Peak input 0.005 / 1.5, actual normalized nonlinear residual |

The small COMP error is an internal mathematical cancellation result at low signal level. It is not a claimed audible accuracy relative to the original. Transient fixtures are measured after control smoothing settles; the test is not a compressor response test. WAV fixtures that exceed full scale are normalized for safe PCM export, so use the CSV's original level measurements for gain comparisons.

The same run measured about 237 ns/float channel-sample and 168 ns/double channel-sample for the fully enabled base-rate engine, approximately 2.27% and 1.61% of one core at 48 kHz stereo. Shared-cloud CPU scheduling, compiler options and processing quality affect these numbers. They exclude the JUCE wrapper and oversampling, and they cannot predict Windows/Ableton instance counts.

The checked-in [numerical report](ValidationArtifacts/report.json), [CSV measurements](ValidationArtifacts/metrics.csv), [harmonic plot](ValidationArtifacts/harmonics.png), [small-signal response plot](ValidationArtifacts/frequency_response.png), [impulse plot](ValidationArtifacts/impulse.png) and [transient plot](ValidationArtifacts/transient.png) record that run. These are generated from this implementation, without reference-product audio. Regenerate them after DSP calibration changes; CPU measurements are not deterministic.

## Host-wrapper checks and Windows acceptance

`Tests/PluginTests.cpp` exercises the actual JUCE processor when plugin builds are enabled. It is responsible for dry/wet alignment and bypass latency, quality switching, oversampling alias rejection, preset/state recall and complete float/double block processing. Consult its executable output and generated measurements for actual results. Core results above must not be presented as host-wrapper validation.

On 2026-10-17, the full native Linux CTest suite passed **2/2 tests**: 12 core groups and **1,583,935 processor assertions**, covering float/double, mono/stereo, the five requested sample rates, twelve factory presets, state restoration, bypass, fixed latency, oversampling and editor creation. The [actual CTest log](ValidationArtifacts/native-ctest.log) is retained. The core suite also passed with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection enabled, without diagnostics; see [sanitizer output](ValidationArtifacts/core-sanitizers.log).

The real compiled Linux VST3 bundle was separately loaded by **pluginval 1.0.4, strictness level 5**, and completed its GUI, audio, state, automation and bus tests with `SUCCESS`. Its [full console log](ValidationArtifacts/pluginval-linux.log) is retained. Pluginval skipped the external Steinberg VST3 validator because no validator executable path was configured; that external validator was not run. `auval` is not applicable to this VST3-only target.

Measured host-wrapper results:

| Quality | Third-harmonic alias at 18 kHz | Reduction versus 1x core | Full-wrapper CPU, one core |
|---|---:|---:|---:|
| 1x core comparison | −7.713 dBFS | — | — |
| 2x | −69.505 dBFS | 61.791 dB | 4.51% |
| 4x | −68.477 dBFS | 60.764 dB | 9.08% |
| 8x | −90.965 dBFS | 83.251 dB | 17.95% |

The alias test uses a deliberately driven 10 kHz sine at 48 kHz, peak input 0.6, +18 dB INPUT and AUTO GAIN off, and measures the folded third harmonic at 18 kHz. This is one reproducible stress signal, not a universal rejection guarantee. Different quality filters can produce a non-monotonic result for a particular folded component. The 1x row uses the raw core; it is not a selectable plugin setting. CPU values include the actual FIR wrapper with stereo float, DUAL/2ND, 48 kHz and 256-sample blocks, measured by wall clock on the shared cloud host; the benchmark loop also fills the sine stimulus buffers. These values are descriptive and do not predict Windows instance counts. See [raw alias measurements](ValidationArtifacts/plugin_oversampling.csv), [phase/CPU measurements](ValidationArtifacts/plugin_phase_cpu.csv), [alias plot](ValidationArtifacts/plugin_alias.png) and [parallel-phase plot](ValidationArtifacts/plugin_phase.png).

All three quality choices, the dry branch, plugin bypass and host bypass share **65 samples** of latency. Small-signal phase tests at 100 Hz–20 kHz measured approximately 4.003° wet-phase difference at 100 Hz, consistent with the intentional 7 Hz DC blocker. At 1 kHz and above the difference was at most 0.402°; wet amplitude and 50% parallel-mix amplitude differed from unity by less than 0.01 dB. At 100 Hz, wet loss was at most 0.030 dB and parallel-mix loss at most 0.021 dB. Exact high-rate integer padding aligns the FIR conversion delay without an allpass phase correction.

The Windows workflow builds the real VST3 bundle with Visual Studio 2022 and runs the executable tests; the Windows run was still pending when these native results were recorded. A release additionally needs Windows 11 installation into `C:\Program Files\Common Files\VST3\`, Ableton Live scan/load, audio and automation at several block sizes, mono/stereo routing, project/preset recall, bypass/quality transitions, HiDPI resizing, mouse/keyboard gestures and a listening pass. Native Linux validation does not establish those Windows/DAW behaviors. Any later Windows results are stated in the top-level delivery notes.

## Optional licensed-reference comparison

Render the **same source** through Ivory Drive and the user-licensed original. Use identical sample rate, mono/stereo format, input drive, output level, dry/wet and control settings; capture enough leading silence and document the reference version and quality. Use uncompressed 16/24/32-bit integer PCM WAV. The analysis tool does not load proprietary plugins or acquire assets.

```sh
python3 Tools/analyze.py \
  --measurements build-dsp/validation \
  --reference /path/to/licensed-reference-output.wav \
  --candidate /path/to/ivory-output.wav \
  --dry /path/to/identical-input.wav \
  --output /path/to/comparison
```

For an isolated stationary sine, add `--fundamental 997` using the actual test frequency. Omit that flag for music, sweeps and impulses. The tool aligns an integer delay within ±100 ms (adjustable), applies one global RMS gain match and reports spectrum, harmonics when applicable, RMS/peak/crest/DC, maximum sample step and the aligned residual. The optional dry stimulus produces cross-spectrum transfer/coherence CSVs; with a nonlinear processor these are stimulus- and level-dependent estimates, not universal LTI frequency responses. A phase/time difference can dominate the residual despite similar subjective tone.

Repeat sine renders over drive levels and frequencies to compare level-dependent distortion and gain. Use impulses and decaying bursts to compare transient peak rounding, and low-level broadband/sweep signals for frequency shaping. Keep numerical and listening observations together. Matching one plot or reducing a waveform residual cannot establish a confirmed 1:1 sonic match.

The tool was checked using an original synthetic three-harmonic sine: a 37-sample delay was recovered exactly and calculated THD matched the known harmonic amplitudes. A self-comparison of the same generated PCM fixture also completed. No real original-product comparison has been performed.
