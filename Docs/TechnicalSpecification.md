# Ivory Drive technical specification

Ivory Drive is an independent saturation effect by Ivory Audio. Its original C++ DSP and vector interface implement the functional requirements supplied for a processor inspired by the Overstayer M-A-S workflow. It contains no extracted proprietary code, product artwork, logo, impulse response or calibration recording. No measured equivalence to the hardware or another vendor's plugin is asserted.

## Sources and confidence

The user supplied the complete “CODEX MASTER PROMPT — OVERSTAYER M-A-S INSPIRED VST3 PLUGIN” and a screenshot of the product's web page. The screenshot establishes visual cues: cream rack panels, large dark knobs, small white switches, green indicators, separate control groups and a lower Extended panel. All distributed graphics are drawn independently.

The following official sources were retrieved and read on 2026-10-17:

| Source | URL | Retrieval result |
|---|---|---|
| Product page | <https://www.softube.com/plug-ins/overstayer-m-a-s> | HTTP 200; readable product descriptions retrieved |
| User manual | <https://www.softube.com/eu/user-manuals/overstayer-mas> | HTTP 200; full HTML manual text and image descriptions retrieved |

Initial requests failed with HTTPS proxy tunnel `403 Forbidden`; access later succeeded on the same date, after environment configuration was revisited. This specification reflects the successfully retrieved text and official images. The manual's [functional block diagram](https://softubestorage.b-cdn.net/manuals/images/Overstayer_M-A-S/image5.jpg), Extended interface image and resonance descriptions were also inspected. The diagram verifies that the dry branch splits before the common input filters, the common filters precede INPUT gain, Extended HP/LP precede its PRE EQ, and AUTO GAIN follows POST EQ. No original artwork is redistributed, and no marketing audio/video was loaded.

The Classic controls, six emphasis descriptions, Extended HP/LP frequency ranges, second-order slopes, four EQ bands, 21 Presence positions, complementary compensation and Extended Emphasis gating below are verified against the official manual. Exact circuit values, mathematical transfer functions, fixed emphasis curves, intermediate frequency positions, oversampling implementation and original preset settings are **not documented there**. The engineering choices below are explicitly independent assumptions and remain candidates for later reference calibration. Windows-only delivery, selectable oversampling, the original preset list and independent branding are requirements supplied by the user rather than claimed original-product specifications.

## Deliverable and processing contract

The release target is Windows 11 x64, VST3, C++20, JUCE, CMake and Visual Studio 2022. Mono and stereo, float and double buffers, and 44.1/48/88.2/96/192 kHz are required. The repository also builds native Linux validation executables to exercise DSP in the available cloud environment; Linux execution does not establish Windows or Ableton Live certification.

One plugin exposes Classic and Extended views backed by the same saturation engine and persistent host parameters. The wet branch runs at a selectable 2x, 4x or 8x rate. Dry mixing and bypass must preserve reported latency. Parameter state, Classic/Extended selection, quality selection and presets must survive project recall.

## Control requirements

| Control | Required behavior | Implementation ownership |
|---|---|---|
| INPUT | Drive nonlinear stages | Wet engine, smoothed in dB |
| MIX | Dry/wet balance 0–100% | Host processor with latency-matched dry signal |
| OUTPUT | Final output gain | Host processor |
| AUTO GAIN | Remove corresponding linear input gain while retaining saturation | Wet engine; independent feed-forward gain inverse, not a compressor or adaptive loudness detector |
| TO DRY | Apply corresponding INPUT gain to dry branch | Host processor |
| DUAL | Engage a second nonlinear stage in series; add harmonic complexity and second-harmonic content | Wet engine; independently calibrated stage with separate state |
| 2ND | Increase even/odd harmonic energy ratio | Wet engine asymmetric nonlinear term |
| LOW CUT | Resonant second-order high-pass near 50 Hz | Common pre-saturation filter |
| LOW SHELF | Approximately +2 dB at 100 Hz | Common pre-saturation shelf |
| HIGH SHELF | Approximately +2 dB at 2 kHz | Common pre-saturation shelf |
| EMPHASIS | Enable pre/post saturation frequency shaping; also gate Extended HP/LP | Both views; Extended lower section uses a common enable |
| Classic emphasis | Six mutually exclusive frequency-dependent saturation profiles | Complementary pre/post filters around nonlinear stages |
| HP | Extended second-order resonant HP, 20 Hz–2 kHz | Pre-saturation filter, independent enable |
| LP | Extended second-order resonant LP, 200 Hz–20 kHz | Pre-saturation filter, independent enable |
| Bass | Extended low shelf near 150 Hz, PRE/POST and COMP | Emphasis engine |
| Presence 1/2 | Two independent peaking bands, 21 positions spanning 50 Hz–20 kHz | Emphasis engine |
| Treble | Extended high shelf near 7.5 kHz, PRE/POST and COMP | Emphasis engine |
| COMP | Corresponding POST becomes complementary to PRE; when off, POST remains independent | Applied per band before/after saturation |
| Bypass | Host-aware bypass with consistent latency | Host processor |

Extended replaces the fixed Classic emphasis profile. The common white input shelf switches and LOW CUT operate in both views, independently of EMPHASIS; Extended adds dedicated HP/LP controls in its lower panel. The official manual explicitly requires Emphasis to be enabled for those Extended lower-section filters as well as its emphasis EQ. COMP does not cancel harmonics generated between the two complementary EQ stages. The plugin exposes 2x/4x/8x quality; the standalone core runs at its supplied rate and permits 1x comparisons in the validation tools.

## Persistent parameter map

The following stable IDs are created by `PluginProcessor.cpp`; these numeric gain ranges and defaults are independent product choices unless a frequency range is documented above. Choice values are zero-based indices. Every parameter has host automation and state serialization.

| IDs | Range or choices | Default |
|---|---|---|
| `input` / `output` | −24…+36 dB / −36…+12 dB | 0 / 0 dB |
| `mix` | 0…100% | 100% |
| `autoGain` / `toDry` | Boolean | on / off |
| `dual` / `second` | Boolean | off / off |
| `lowCut` / `lowShelf` / `highShelf` | Boolean | all off |
| `emphasis` / `extended` | Boolean | on / off |
| `classicMode` | Classic, Low Bandwidth, Low Presence, High Presence, Bass Distortion, Treble Distortion | Classic |
| `hpOn` / `lpOn` | Boolean | off / off |
| `hpFreq` / `lpFreq` | 20…2000 Hz / 200…20000 Hz | 50 / 16000 Hz |
| `hpRes` / `lpRes` | 0…1 | 0 / 0 |
| `bassPre`, `p1Pre`, `p2Pre`, `treblePre` | −12…+12 dB | 0 dB |
| `bassPost`, `p1Post`, `p2Post`, `treblePost` | −12…+12 dB; effective value follows inverse PRE while COMP is on | 0 dB |
| `bassComp`, `p1Comp`, `p2Comp`, `trebleComp` | Boolean | all on |
| `p1Freq` / `p2Freq` | 21 fixed frequencies, indices 0…20 | indices 10 / 14 |
| `oversampling` | 2x, 4x, 8x | index 1, 4x |
| `bypass` | Boolean | off |

## Independent engineering assumptions

The main transfer is a blend of rational soft clipping and an algebraic saturator, with an asymmetric even term, low-pass-filtered nonlinear residual feedback and two separately calibrated stages. This is circuit-inspired phenomenological modeling; it is not a circuit reconstruction. A 7 Hz DC blocker follows the nonlinear/post-EQ section. No compressor attack/release envelope controls saturation.

The six profiles use four filters each: low shelf, two peaks, high shelf. Their PRE gain and frequency arrays are defined in `Source/DSP/EmphasisEngine.h`; POST applies complementary gains. Labels correspond to the requested musical intentions, not verified original curves:

| Selection | Intent from supplied requirements | Independent PRE gains, shelf / peak 1 / peak 2 / shelf (dB) |
|---|---|---|
| Classic | Midrange and upper-frequency saturation | −3 / +4 / +2 / −1 |
| Low Bandwidth | More bass and treble saturation than mids | +5 / −5 / +1 / +5 |
| Low Presence | Upper-mid presence saturation | −1 / +7 / +2 / −1 |
| High Presence | Upper-presence harmonic brightness | −2 / +3 / +7 / +1 |
| Bass Distortion | Thin bass via stronger bass distortion while comparatively preserving highs | +9 / +3 / −3 / −5 |
| Treble Distortion | Treble-focused smoothing/saturation | −4 / +3 / +5 / +5 |

Presence positions use `f(i) = 50 × 400^(i/20)` for `i = 0…20`, giving endpoints 50 Hz and 20 kHz. The supplied requirements did not establish the original intermediate centers. Filters clamp tuning to 0.45 times the internal sample rate for numerical stability near Nyquist; the UI's requested position remains stored.

Extended resonance maps linearly from `Q = 1/√2` to `Q = 1.3`; this provides approximately +3 dB peak resonance at maximum. The manual's filter-response image descriptions state a smooth minimum, approximately 1 dB resonance in the middle and 3 dB at maximum. The exact original Q and taper are not specified. Extended emphasis peaks use Q 0.8. Shelf damping uses Q `1/√2`. Parameter smoothing, feedback constants, oversampling filters and latency policy are described in [DSP.md](DSP.md).

The official manual also states that the hardware's safety-clipping op-amp circuit was omitted from Softube's plugin. Ivory Drive's numerical bounding is an independent stability measure, not an emulation of that omitted hardware circuit. Amp Room and Modular versions described by Softube are outside this user's Windows VST3 target.

## Interface, state and performance requirements

The original rack interface uses scalable vector drawing, real rotary controls, Shift fine adjustment, wheel adjustment, double-click defaults, parameter value display and illuminated state switches. Input/output meters reflect audio peak measurements; the saturation meter measures the difference introduced by nonlinear processing. Meter ballistics are visual smoothing of measured quantities.

The twelve requested factory preset names are retained as descriptive names and use original parameter settings. User presets and host state share the complete parameter state. No file IO, locks or allocation should occur in normal audio processing; preparation constructs storage for all quality choices. Changing quality should use already prepared branches and retain a stable reported latency.

## Acceptance and remaining calibration

The executable tests and measurement tools are described in [Validation.md](Validation.md). Passing a core regression test establishes the stated behavior in that implementation; it does not establish a match to the reference product. Windows VST3 hosting, Ableton session recall, GUI gestures/HiDPI, OS-specific installation and real-time behavior must be exercised in Windows before a release claim.

Reference calibration requires user-rendered audio from a licensed original with identical input, settings, sample rate and capture conditions. The optional comparison tool reports harmonics, level/gain, crest/transient measures, integer-delay-aligned residuals and stimulus-dependent transfer estimates. Such data can guide future changes to the public calibration constants without importing proprietary implementation assets.
