# Motion Graphics Toolkit for Adobe After Effects

A native After Effects **effect plug-in** (C++ / After Effects SDK), not a script, not a CEP
panel. Eight effects and a set of keyframe generators aimed at motion graphics work: easing,
auto-animation, staggering, bounce, wiggle, glow, warp and the rest of the tools you reach for
between the first and last keyframe.

```
make            # core library + both test suites, no SDK needed
make plugins AE_SDK_ROOT=/path/to/sdk   # the eight .aex / .plugin bundles
```

---

## What is in the box

Eight effects, each its own plug-in with its own PiPL, all sharing one core library:

| Effect | Match name | What it does |
|---|---|---|
| **MGTK Feedback Echo** | `MotionGraphicsToolkit_FeedbackEcho` | Accumulates the layer across frames through a transform, colour shift and blend. Zoom tunnels, trails, infinite echoes. |
| **MGTK Chromatic Split** | `MotionGraphicsToolkit_ChromaticSplit` | Separates colour channels radially, linearly, by zoom, by spin or with a barrel warp. Independent per-channel amounts. |
| **MGTK Anamorphic Glow** | `MotionGraphicsToolkit_AnamorphicGlow` | Threshold-driven bloom with a sampled anamorphic streak pass, multi-component tints and an optional channel split. |
| **MGTK Fractal Warp** | `MotionGraphicsToolkit_FractalWarp` | Fractal-noise displacement, swirl, pinch and domain warping with animatable evolution and a reproducible seed. |
| **MGTK Kaleidoscope** | `MotionGraphicsToolkit_Kaleidoscope` | Mirrored, rotated or quilted radial symmetry with animatable segments and centre. |
| **MGTK Halftone Pro** | `MotionGraphicsToolkit_HalftonePro` | Dot, line, cross, diamond, square and concentric screens in monochrome, RGB or CMYK with per-channel screen angles. |
| **MGTK Slit Scan** | `MotionGraphicsToolkit_SlitScan` | Time displacement: slices, blends, echoes and motion-driven offsets across up to 24 frames of history. |
| **MGTK Pixel Sort** | `MotionGraphicsToolkit_PixelSort` | Datamosh pixel sorting by brightness, hue, saturation or a channel, with thresholding, run capping and stretching. |

Plus `include/mgtk/keyframes.hpp`: easing retiming, stagger, overshoot, bounce and
deterministic wiggle, implemented and unit-tested. See [docs/AEGP.md](docs/AEGP.md) for the
honest status of the AEGP that will expose them in the Animation menu.

**Every effect is 32-bit float capable, 16-bit capable, and renders identically on a re-render
of the same frame** — the last one matters more than it sounds, and the section on determinism
below explains why.

---

## Requirements

| | |
|---|---|
| Compiler | Any C++17 compiler. Tested with GCC 12 and MSVC 2019+. |
| Build | `make` (Linux/macOS) or CMake 3.16+ (any platform). |
| After Effects | CS6 (11.0) or later for the plug-ins. 13.8 / After Effects 13.8-era spec is targeted. |
| After Effects SDK | Needed **only** to build the plug-ins. [Download it from Adobe.](https://developer.adobe.com/after-effects/) |

The core library, both test suites and the PiPL generator build with nothing but a compiler.
That is deliberate: it means the pixel algorithms can be developed, tested and debugged without
an After Effects installation, which is how most of this code was written.

---

## Building

### Core library and tests (no SDK)

```bash
git clone https://github.com/tharun123-jpg/test222
cd test222
make          # build
make test     # build and run
```

```
125 passed, 0 failed, 78872 checks total   <- core algorithms
 21 passed, 0 failed,   197 checks total   <- keyframe generators
 15 passed, 0 failed,  1757 checks total   <- the After Effects glue
```

`make test-one F=halftone` runs only the tests whose name contains `halftone`.

### The After Effects plug-ins

```bash
make plugins AE_SDK_ROOT=/path/to/AfterEffectsSDK
```

This builds eight `.aex` files (macOS: `.plugin` bundles) into `build/plugins/`, and regenerates
`resources/pipl/*.r` first. The PiPL step is not optional and not cosmetic — see
[the section on flags](#the-pipl-and-why-it-is-generated).

With CMake instead:

```bash
cmake -B build -DAE_SDK_ROOT=/path/to/AfterEffectsSDK
cmake --build build
```

Attaching the PiPL resource to each binary is a platform step that uses Adobe's own tools, and
it differs enough between Windows and macOS to be worth its own page:
**[docs/BUILDING.md](docs/BUILDING.md)** has the exact commands for both.

### Installing

| Platform | Where the plug-ins go |
|---|---|
| macOS | `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/` |
| Windows | `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\` |

`scripts/install.sh` (macOS/Linux) and `scripts/install.bat` (Windows) copy the built bundles
there. After Effects must be restarted; the effects then appear under
**Effects & Presets → Motion Graphics Toolkit**.

---

## Using it

Every effect lives in its own submenu: **Effects & Presets → Motion Graphics Toolkit**. Drag one
onto a layer and open Effect Controls.

A few notes that save time:

* **Feedback Echo** writes its accumulator into the effect's sequence data, so it is saved with
  the project and survives a close and reopen. It also has no visible effect on the very first
  frame of a composition — there is nothing to echo yet. Blend mode defaults to **Add** rather
  than Normal for exactly that reason: on an opaque layer, Normal hides the echo completely.
* **Slit Scan** checks out up to 24 past frames. Each one is a full render of the layer at an
  earlier time, so the Frames slider is a genuine performance dial, not a decoration.
* **Anamorphic Glow**'s Quality control trades streak sampling density, not resolution: Draft
  halves the number of samples per streak, it does not soften the result.
* **Pixel Sort**'s Threshold Low and High are the interesting controls. Sorting the whole frame
  is rarely what you want; sorting only the mid-tones is where the effect lives.

---

## How it is put together

```
include/mgtk/          public core headers -- pure C++17, zero Adobe includes
  image.hpp            Image / ImageView, sampling, wrap modes
  math.hpp             scalars, vectors, hashing, the 31 easing curves
  noise.hpp            value / gradient / simplex / worley / fBm, 2D and 3D
  blur.hpp             box, gaussian, directional, resampling
  blend.hpp            22 blend modes, straight-alpha compositing
  effects.hpp          parameters and entry points for the eight effects
  keyframes.hpp        easing, stagger, overshoot, bounce, wiggle
  version.hpp          version, match names, SDK spec version

src/core/              the implementation, and all of the interesting code
src/ae/                the After Effects glue
  mgtk_ae.hpp/.cpp     worlds <-> images, parameter checkout, progress and abort
  effect_spec.hpp      what an effect is: params, marshalling, flags
  registry.cpp         the table of all eight effects -- the source of truth
  param_table.cpp      ParamSpec -> PF_ParamDef
  dispatcher.cpp       EffectMain: the command selector loop
  plugins/             eight ~6-line translation units, one per effect
  aegp/                the AEGP
resources/pipl/        generated PiPL resource files
tests/                 core tests, keyframe tests
tests/ae_shim/         a stand-in After Effects SDK and host
tools/gen_pipl.cpp     writes the PiPL resources from the registry
```

Three decisions shape everything else.

### 1. The effects are pure C++ with no Adobe dependency

`src/core/` does not include a single AE header. An effect is a function:

```cpp
void apply_anamorphic_glow(const Image& src, Image& dst,
                           const AnamorphicGlowParams& p, const RenderContext& ctx);
```

That is the whole interface. It is what makes the test suite possible, and the test suite is
what makes the effects trustworthy: 78,872 assertions about pixel values, run in under a second,
with no After Effects anywhere in sight.

The glue layer is then genuinely thin. `src/ae/registry.cpp` says what the parameters are and
how to read them; `src/ae/dispatcher.cpp` runs the SmartFX protocol; `src/ae/plugins/*.cpp` are
six lines each.

### 2. Determinism is a hard requirement, not a nicety

After Effects caches rendered frames to disk and renders multi-frame sequences out of order. An
effect that uses `rand()` or a thread id produces a different image for the same frame depending
on the order the cache was filled — the classic "my render looks different from my preview" bug.

Every random number in this library comes from an explicit hash of integer coordinates:

```cpp
float offset = rand_signed_2i(line, position);   // deterministic on (line, position)
```

No `rand()`, no time seeds, no thread-local state. Two renders of the same frame are identical,
in any order, on any machine.

### 3. Colour is straight-alpha inside, premultiplied at the boundary

After Effects hands effects premultiplied pixels. Blurring premultiplied pixels drags the matte
into the colour, which is where the dark halo around every semi-transparent edge comes from.
The core therefore works in straight alpha and clips its output only where it has to, and
`mgtk_ae.cpp` does the premultiplication on the way back out. The effect that must preserve the
source matte exactly — Halftone Pro, whose whole job is to replace colour — asks for that
explicitly through `AlphaPolicy::StraightKeepSourceAlpha`.

---

## Bit depth

All eight effects are **SmartFX** plug-ins. They claim `PF_OutFlag_DEEP_COLOR_AWARE` and
`PF_OutFlag2_SUPPORTS_SMART_RENDER`, which means:

* **8-bit and 16-bit projects** render through the same code path.
* **32-bit float projects** keep their highlights. Values above 1.0 stay above 1.0 — the glow
  does not clip a 4.0 highlight to white, and the test suite checks the round trip is exact in
  float and within one 8-bit step in 8-bit.

Tonal thresholds (`Threshold`, `Knee` and friends) are interpreted in a perceptual space when the
project is linear, so a Threshold of 1.0 means the same thing in an 8-bit project as in a 32-bit
one. That is what `RenderContext::input_linear` is for.

---

## The PiPL, and why it is generated

A PiPL is the resource that tells After Effects what is inside a binary. One of its fields is the
effect's global out-flags, and those must be **byte-identical** to what the plug-in writes in
`PF_Cmd_GLOBAL_SETUP`. If they differ, After Effects refuses to load the plug-in with:

> the values in the pipl must correlate to the values set in the plug-ins global setup call

Two things make this hard to get right by hand:

1. `PF_OutFlag_*` are **C enums, not `#define`s**, and a `.r` file is preprocessed by a resource
   compiler. Writing `PF_OutFlag_USE_OUTPUT_EXTENT | PF_OutFlag_DEEP_COLOR_AWARE` in a `.r` file
   expands to `0 | 0`, so the resource either fails to parse or registers a plug-in that claims no
   flags at all. The numbers have to be literals.
2. Nothing keeps those literals in step with the code that sets the flags at runtime.

So both come from one place. `src/ae/registry.cpp` is the single table of effects, and
`tools/gen_pipl.cpp` walks it and prints the resource files:

```bash
make gen-pipl     # regenerate resources/pipl/*.r from the registry
```

The out-flags are printed from the same constants `PF_Cmd_GLOBAL_SETUP` assigns, so the two
cannot drift apart. If you add an effect, add it to the registry and run `make gen-pipl`.

---

## Testing

```bash
make test        # everything: core, keyframes, and the AE glue
make test-one F=glow
```

Three suites:

**Core** (125 tests, 78,872 checks) — the pixel algorithms. Sampling and wrap modes, blur
conservation, blend-mode identities, noise determinism and continuity, and for each effect the
properties that actually matter: that it is deterministic, that it is finite at extreme
parameters, that it survives 1×1 and 0×0 images, that its geometry is what it claims (the
kaleidoscope really is n-fold symmetric), and that its parameters do what their names say.

**Keyframes** (21 tests) — the auto-animation generators. Mostly about what must *not* move: the
first and last keyframes are poses the animator chose and no generator is allowed to touch them,
a bounce must land exactly on the final value, and a wiggle must produce the same numbers every
time the project is reopened.

**Glue** (15 tests) — the After Effects-facing code, run against a stand-in host in
`tests/ae_shim/` that speaks AE's calling convention. It drives the real dispatcher and the real
effect registry through GlobalSetup → ParamsSetup → SequenceSetup → SmartFX pre-render/render
over synthetic layers, and checks the things that would otherwise only fail inside After Effects:
that the out-flags match the PiPL, that every parameter the registry declares can be read back,
that a cancelled render returns the interrupt error, and that the premultiply round trip does not
distort a semi-transparent ramp.

### What the tests cannot catch

`tests/ae_shim/` is a test double, not the SDK. It models the API surface this project uses, with
field layouts taken from Adobe's own headers. It cannot catch a suite acquired with the wrong
version, a host that behaves differently from the documentation, or anything about GPU rendering
and real caching. Those need the SDK build and a copy of After Effects — which is why
`docs/BUILDING.md` keeps the SDK build to a single command.

---

## Performance

The effects are single-pass over the image except where the effect is inherently multi-pass
(glow's bloom pyramid, slit scan's frame checkouts). A 1920×1080 frame through the glow at
default settings is a few million samples; through the pixel sort it is one sort per row.

Two things keep it honest:

* The glow blurs at reduced resolution while the blur radius stays above four pixels. Beyond
  that point the extra resolution is not visible in the result, and the cost is quadratic.
* Every effect polls `ctx.aborted()` once per row block and reports progress through
  `PF_PROGRESS`, so a long render stays cancellable and After Effects draws a real progress bar.

---

## Limitations

Worth knowing before you file a bug:

* **Coordinate space.** The core works in pixel-index coordinates — pixel `(i,j)` *is* the point
  `(i,j)`. After Effects describes a layer point `(i+0.5, j+0.5)` as the centre of pixel `(i,j)`,
  so there is exactly one half-pixel conversion, at the boundary in `mgtk_ae.cpp`.
* **Slit Scan at the start of a composition.** There are no frames before the first one, so the
  effect falls back to the oldest frame it was given. That is a correct reading of the request,
  but it means the first Frames frames are not what they will be later.
* **Feedback Echo under multi-frame rendering.** The accumulator is per-effect-instance sequence
  data, and After Effects does not guarantee frames are rendered in order. The effect does not
  claim `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` for that reason, so it renders single-threaded.
* **The AEGP is a shell.** It loads, registers its menu, and answers the About command. The
  keyframe-editing commands it will carry are not implemented; the generators they will drive are
  finished and tested, and [docs/AEGP.md](docs/AEGP.md) lists exactly what is left. Shipping
  calls against AEGP suites that cannot be compiled here would have looked more finished and been
  less useful.
* **No GPU path.** `PF_Cmd_SMART_RENDER_GPU` is not implemented, so the effects run on the CPU in
  a GPU-accelerated project. That is a supported configuration, just not a fast one.

---

## License

MIT. See [LICENSE](LICENSE). Adobe, After Effects and Premiere Pro are trademarks of Adobe Inc.
This project is not affiliated with or endorsed by Adobe.
