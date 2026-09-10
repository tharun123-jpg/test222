# The AEGP: status, and exactly what is left

## The short version

The keyframe generators are **finished, tested and in the core**. The AEGP that will expose them
in After Effects' Animation menu is **a working shell**: it loads, registers its menu, and answers
its About command.

The layer in between — reading keyframes out of an AEGP stream and writing them back — is
deliberately not written. This page explains why, and lists exactly what it needs.

## What is finished

`include/mgtk/keyframes.hpp` and `src/core/keyframes.cpp` implement five generators, and
`tests/test_keyframes.cpp` covers them with 21 tests / 197 checks:

| Function | What it does |
|---|---|
| `retime_easing(keys, curve, strength)` | Re-spaces the intervals between keyframes according to any of the 31 easing curves. The keyframes themselves do not move; only the pacing changes. |
| `stagger_keyframes(keys, step, ping_pong)` | Offsets each keyframe in time by its index, so a row of elements ripples instead of moving together. Ping-pong folds the offset back so the row arrives level. |
| `apply_overshoot(keys, overshoot, anticipation)` | Replaces the value path with a damped settle that lands exactly on the final keyframe. |
| `apply_bounce(keys, bounces, decay, height)` | Replaces the value path with decaying parabolic arcs at the physically-derived `sqrt(2h/g)` intervals. |
| `apply_wiggle(keys, amplitude, frequency, seed, octaves)` | Adds deterministic noise: identical on every reopen, because it is a function of the seed and the keyframe index, never of the clock. |

These take `std::vector<mgtk::Keyframe>` — a plain `{float time; float value;}` — and know nothing
about After Effects. That is what makes them testable, and the tests are mostly about the things
that must not move: the first and last keyframes are poses the animator chose, a bounce has to
land exactly on the final value, and neighbouring keyframes must not wiggle in lockstep.

## What is not finished, and why

Reading and writing keyframes in an AEGP goes through four suites — `AEGP_StreamSuite`,
`AEGP_DynamicStreamSuite`, `AEGP_KeyframeSuite` and `AEGP_UtilitySuite` — whose function names,
version numbers and argument orders differ across SDK generations and are documented unevenly.

Every other piece of this project is compiled and exercised by the repository's own test suite
before it is committed. That is not true of the AEGP suites: they exist only inside After
Effects, they have no stand-in, and no amount of care substitutes for one compile. Writing that
layer from documentation would have produced a file that looks finished, does not build, and
costs whoever tries it first a debugging session in a domain with no debugger. So it was left
out, and this page written instead.

## What the missing layer needs

A single file, `src/ae/aegp/keyframe_edit.cpp`, with roughly these four steps. Everything in
italics is the part that must be verified against the SDK you have.

1. **Find the streams to operate on.** For each selected layer, get its stream group, then the
   child streams of interest. The four that matter for these tools are the transform streams —
   position, scale, rotation, opacity.
   *The calls are `AEGP_GetNewStreamRefForLayer` for the layer's stream group, and either
   `AEGP_GetNewStreamRefByIndex` or `AEGP_GetNewStreamRefByMatchname` for its children
   (`"ADBE Position"`, `"ADBE Scale"`, `"ADBE Rotate Z"`, `"ADBE Opacity"`). Check the
   `AEGP_StreamSuite` version in your SDK — the `New...` variants are the ownership-correct ones,
   and every one of them must be paired with `AEGP_DisposeStream`.*

2. **Read the keyframes into a `std::vector<mgtk::Keyframe>`.**
   *`AEGP_GetStreamNumKFs` for the count. For each index: `AEGP_GetKeyframeTime` with
   `AEGP_LTimeMode_CompTime`, then the keyframe's value. Times arrive as `A_Time`
   (`{value, scale}`), so divide to get seconds — the core's generators take seconds on purpose,
   because a wiggle specified in frames would change its look with the composition's frame rate.
   Values arrive as `AEGP_StreamValue2`, a union: read the member that matches the stream's type
   (`AEGP_GetStreamType`).*

3. **Run the generator.** One call into the core. This is the whole point of the split:

   ```cpp
   mgtk::retime_easing(keys, mgtk::Easing::CubicInOut, 1.0f);
   ```

4. **Write the values back.**
   *Note that steps 2 and 4 do not cancel: easing and stagger both move keyframes in time, so the
   index you read is not necessarily the index you should write. Read every time and value first,
   then set values against the times you recorded, and drop any keyframe that the operation pushed
   out of the work area. Values go back as `AEGP_StreamValue2` with the same union member the
   stream already used — After Effects will accept a mismatched member and then render nonsense.*

5. **Wrap it in a command.** `src/ae/aegp/mgtk_aegp.cpp` already does this part: it allocates
   command ids with `AEGP_GetUniqueCommand`, adds them to the Animation menu with
   `AEGP_InsertMenuCommand`, and dispatches them in `AEGP_PluginCommandHook`. Adding a command is
   three lines there plus a case in the handler.

A worthwhile extra, once the above works: read the current selection's keyframes, and if there
are fewer than two, report that in the status bar rather than silently doing nothing.

## Testing it once it exists

The generator half needs no After Effects and is already covered. The glue half can be covered
the same way the effects are: `tests/ae_shim/` already fakes the effect side of the SDK, and the
same technique works for the AEGP side — a handful of function pointers matching the suite
layouts, a fake keyframe list, and a check that what comes back out of the "stream" is what
`apply_bounce` produced. That is the shape of test worth writing, and it is the reason the
generators were kept free of any AE types in the first place.
