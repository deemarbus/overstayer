# Ivory Drive DSP design

The signal model is independently implemented and deliberately exposes its calibration constants in source. It fulfills the requested pre/nonlinear/post workflow; the transfer and filters have not been fitted to recordings of the reference product.

## Signal flow

```text
Input ──┬── latency-matched dry ── TO DRY gain ─────────────┐
        │                                                 │
        └── oversampling up ── common LOW CUT/shelves ── INPUT
                           ── Extended HP/LP ── PRE EQ
                           ── primary nonlinear stage ── optional DUAL stage
                           ── POST EQ ── DC removal ── AUTO GAIN
                           ── oversampling down ── latency padding ── MIX
                                                              ── OUTPUT ── output
```

The core `SaturationEngine<Sample>` owns wet-path gain, filtering, nonlinear state and activity measurements. It processes one mono/stereo channel sample at the internal rate. The JUCE processor owns oversampling, output gain, TO DRY, dry/wet mixing, host bypass, host parameter snapshots and latency reporting. Float and double engine instantiations keep their own independent channel state.

## Nonlinear stages

`NonlinearStage.h` blends two odd-symmetric transfer families:

```text
R(x) = x (27 + x²) / (27 + 9 x²), with its argument limited to ±3
A(x) = x / sqrt(1 + x²)
O(x) = (1 − blend) R(x) + blend A(x)
E(x) = asymmetry × x² / (1 + x²)
```

Small-signal gain is normalized by the stage drive. A one-pole-smoothed nonlinear residual feeds back into the next sample. Residual feedback adds limited history-dependent behavior without an amplitude detector or compressor envelope. The two series stages use different drive, blend, asymmetry, feedback and residual bandwidth constants:

| Constant | Primary | Secondary |
|---|---:|---:|
| Drive | 0.78 | 1.12 |
| Algebraic blend | 0.36 | 0.62 |
| Normal asymmetry | 0.008 | 0.020 |
| Additional asymmetry at 2ND | 0.205 | 0.21 |
| Residual feedback | 0.075 | 0.13 |
| Residual low-pass | 2400 Hz | 680 Hz |

These are original calibration choices. DUAL adds the second stage's changed transfer and memory; it is not an input/output gain preset. The second stage runs continuously so its memory remains ready during engagement. DUAL and 2ND use 10 ms smoothing. The activity meter accumulates the largest normalized nonlinear residual `abs(output − input)/(0.05 + abs(input))`, bounded to 0–1, with secondary-stage activity included when DUAL is selected.

## Frequency emphasis and filters

`EmphasisEngine.h` places four second-order filters before saturation and their post filters after saturation in reverse order. In Classic, six original profiles set shelf/peak gains, center frequencies and Q. In Extended, the user controls PRE, POST, COMP and two Presence positions. COMP applies negative PRE gain to the corresponding POST filter. The small-signal complementary transfer cancels; nonlinear components generated between the two sections remain shaped differently.

`SmoothBiquad` uses a topology-preserving state-variable implementation with double integrator states and double coefficient calculations, including for float audio. Frequency and Q smooth logarithmically and EQ gain smooths in dB, each with a 20 ms time constant. Designs update at most every 16 samples; coefficient movement has a 3 ms time constant. Positive tuning/damping coefficients and shared filter state avoid swapping incompatible direct-form state during automation. Parameter calls belong on the audio thread via the processor snapshot.

Extended HP/LP use second-order responses with Q 0.7071–1.3. Extended EMPHASIS disables both these lower-section filters and the emphasis EQ, as required by the official manual. The common LOW CUT is 50 Hz, Q 0.88, and the common auxiliary shelves are +2 dB at 100 Hz and 2 kHz; all three white input switches work in both views independently of EMPHASIS. Extended bass/treble emphasis shelves are 150 Hz and 7.5 kHz. These are independent engineering implementations of the documented behavior; Q and exact curve shape are original choices.

## Gain and numerical behavior

INPUT uses 16 ms dB smoothing. AUTO GAIN multiplies the wet result by the inverse of the same smoothed INPUT gain, with a smoothed enable amount; it removes linear drive gain rather than measuring loudness. It retains saturation-induced gain reduction and color. It is not LUFS normalization.

A 7 Hz DC blocker removes steady offset from asymmetric saturation. Independent state prevents crosstalk. Non-finite input is replaced by silence, the wet input is bounded before drive, and non-finite output is suppressed. Tiny states/output below `1e-30` are flushed to zero. The processor additionally uses JUCE denormal protection. These limits protect numerical stability and are not an output limiter; aggressive EQ can still produce output over 0 dBFS.

Filter/parameter smoothing reduces normal automation discontinuities; finite-output stress testing does not prove every possible host automation curve is inaudible. Quality changes, bypass, state replacement and view transitions also require host-level listening and integration tests.

## Oversampling and latency

The host wrapper constructs JUCE high-quality equiripple half-band FIR oversampling branches for 2x/4x/8x. Linear-phase FIR conversion supports dry/wet phase alignment at the cost of higher latency and CPU than the IIR alternative. Processing filters are prepared at each branch's internal rate. The wrapper pads each branch to a common fixed latency, matches dry delay to that latency and reports that stable value to the host. Steady-state processing runs the selected branch. A quality change resets the preallocated incoming branch, warms it for 30 ms plus latency, then crossfades for 30 ms while the outgoing branch remains active. Later selections are queued. This preserves real-time bounded storage and avoids changing the reported latency midstream. Integration tests measure latency, mixing and alias rejection in the actual wrapper; standalone core tests deliberately do not claim to test oversampling.

Common latency padding aligns the linear-phase oversampling conversions across quality choices and the clean dry signal. The wet path still contains intentional emphasis/filter/DC-removal phase and nonlinear changes; the plugin does not claim dry and saturated wet signals are identical. Host-wrapper phase tests distinguish conversion delay from these processing effects. A core engine supplied with the base sample rate permits comparison with nonlinearity without rate conversion; that 1x comparison is not exposed as a plugin quality setting.

## Calibration workflow

Use stationary coherent tones at multiple levels, multitone material, sweeps, impulses and music rendered through both products under controlled conditions. Keep PRE/POST, gain, quality and dry/wet settings explicit. Measure harmonic ratios separately from overall RMS gain. Change original stage/profile constants against those observations and rerun the regression/integration tests. Never infer exact circuit equivalence or a confirmed 1:1 match from matching a single spectrum.
