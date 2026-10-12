# 07 — The viewer

**What:** `tools/viewer.cpp`, a raylib window that plays a seeded match ten
ticks a second and draws the map, the units, their health and the state hash;
since note 08 it also takes orders from a person, and since note 09 Orange is
the AI. The layout is a top bar, the 640 px board and a 260 px panel of unit
cards and buttons; text is drawn at 10 or 20 px only, because raylib's
default font is a 10 px bitmap and whole multiples keep it crisp.
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
to the screen width; on a 375 px phone the 900 px canvas fits without
horizontal scrolling, at the cost of small text. The keyboard controls do
nothing on a phone; the buttons and the board take a finger.

**Pointer input, measured in the browser.** The HUD buttons take a mouse
and a finger. Three things turned out to be true only after measuring them
with a debug readout drawn on the canvas (`-DLOCKSTEP_DEBUG_INPUT`):

- Emscripten's GLFW port already maps a CSS-scaled canvas back to canvas
  pixels: at a 343 px wide phone layout the pointer over the pause button read
  (62, 67) in canvas space without any correction. A `SetMouseScale` call I
  had added on top of that would have scaled twice; it is gone.
- raylib's web backend maps a touch to the mouse *position* but never to the
  mouse *button* (`EmscriptenTouchCallback` in `rcore_web.c`), so
  `IsMouseButtonPressed()` never fires on a phone. A click is now the rising
  edge of `IsMouseButtonDown() || GetTouchPointCount() > 0`, one edge detector
  for both, so a mouse click cannot count twice.
- A synthetic click whose down and up land inside one frame is invisible to
  frame-based edge detection. The browser automation's instant clicks "proved"
  the buttons dead three times; a held press showed them working. Measured
  afterwards: a 1.5 s mouse press at desktop width gave exactly one press and
  paused the match; a 1.5 s touch at phone width did the same.

**Questions this answers in an interview**
- Where may a lockstep game use float? (Anywhere its result is not part of the
  simulated state: rendering, interpolation, audio.)
- How do you run a game loop in the browser?
- How do you keep a demo page in step with the code? (Build it in CI.)
