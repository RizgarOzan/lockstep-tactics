# 07 — The viewer

**What:** `tools/viewer.cpp`, a raylib window that plays a seeded match ten
ticks a second and draws the map, the units, their health and the state hash.
The same file builds natively and, with Emscripten, into a ~0.3 MB page that
GitHub Pages serves (`.github/workflows/pages.yml` builds it from source on
every push to main).

**It is opt-in.** `-DLOCKSTEP_VIEWER=ON` pulls in raylib 6.0. The determinism
CI does not set it, so a rendering dependency can never break or slow the
check that matters.

**Float is allowed here, and only here.** Units move a quarter of a tile per
tick, which looks jumpy at ten ticks a second, so the viewer interpolates
between the previous and the current position with `float`. That float only
decides where a circle is drawn; nothing it computes is written back into the
`World`. Note 06 measured what float does inside the simulation; on the
render side it can differ between machines all it likes, because nothing
compares pixels.

**The page shows the hash on purpose.** It is the same `hash()` the CI
compares. Checked once: the browser build paused at tick 245 of seed 1 showed
`0b99723fda2ac4bc`, and `lockstep record 1 245` under gcc on Windows ends on
the same value.

**Browser specifics.** A browser cannot run a blocking `while` game loop, so
under Emscripten the frame function is handed to `emscripten_set_main_loop`;
natively the same function runs in an ordinary loop. A small custom shell page
(`tools/shell.html`) replaces Emscripten's default one and scales the canvas
to the screen width; on a 375 px phone it fits without horizontal scrolling.
The keyboard controls do nothing on a phone yet; the match still plays.

**Questions this answers in an interview**
- Where may a lockstep game use float? (Anywhere its result is not part of the
  simulated state: rendering, interpolation, audio.)
- How do you run a game loop in the browser?
- How do you keep a demo page in step with the code? (Build it in CI.)
