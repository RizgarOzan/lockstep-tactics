# 10 — Two browsers, one match

**What:** `core/include/lockstep/net.hpp`, the lockstep protocol as a class
with no sockets in it, and the *Play a friend* box on the page. One player
creates a room and gets a six-letter code, the other joins with it, and the
two browsers play each other: Blue in one, Orange in the other, the same
seed, the same simulation, and both sides compare state hashes as they go.

## The protocol

Every tick, each side sends one frame with its orders for the tick `delay`
ticks ahead (three by default, 300 ms at ten ticks a second):

```
f <player> <tick> <command count> <last tick run> <hash after it>
c <player> <unit> <order> <target_x> <target_y> <target_id>     × count
```

A tick runs only when both sides' orders for it are in. The first `delay`
ticks run with nobody's orders, which is what fills the pipeline. Orders are
ordinary `Command`s, the same lines as a replay file, so a match played by
two people is recorded and saved exactly like one against the AI, and
`lockstep verify` checks it anywhere.

The frame also carries the hash after the last tick that side ran. The
receiver compares it with its own hash for that tick, and the first
disagreement is reported as the desync tick; the viewer paints it over the
board. So a desync is seen within `delay` ticks of happening, by both sides,
with the tick number already known, and the saved replay plus `lockstep
dump` on both machines names the field.

`Lockstep` is pure: `submit()` returns the frame text, `receive()` takes
one, `next()` returns both sides' orders or nothing. `tests/test_net.cpp`
runs two of them against each other for 200 ticks with random orders and
checks every hash matches, checks a tick waits for the peer and never runs
twice, and injects a drift after tick 20 to see both sides name tick 21.

## The transport

The page only has to deliver text frames in order. It uses a WebRTC data
channel (ordered, reliable) between the two browsers, so the orders never
touch a server. The one thing WebRTC cannot do alone is the handshake, and
GitHub Pages cannot run a signalling server, so the offer, the answer and
the ICE candidates go through a topic on [ntfy.sh](https://ntfy.sh), a
public pub/sub relay: the topic name is `lockstep-tactics-<room code>`,
the host subscribes over WebSocket, the guest publishes `join`, the host
answers with the offer and the seed, the guest with the answer, and both
trickle candidates. Six or seven small messages, then the WebSocket is
closed and ntfy is out of the picture. A `?relay=` query parameter points
the page at another ntfy server. A STUN server (Google's public one) tells
each browser its address; there is no TURN, so two browsers behind strict
NATs will fail to connect, and the page says so.

A background tab's `requestAnimationFrame` runs at a crawl, so the two
viewers were tested one fronted at a time: each catches up with the ticks
the other had already submitted, orders given in one appear in the other,
and the hashes stay equal. The viewer stops a long stall from turning into
a blur by running at most half a second of due ticks after one.

## What this proves, and what it does not

It proves the determinism claim end to end: two separate WebAssembly
instances, fed the same orders in the same order, produce the same hash
every tick, and if they stopped doing so the match would say so within
300 ms. It does not prove anything about MSVC against WebAssembly in real
time; that is what the CI and the replay files are for.

Input delay is the simplest lockstep there is. No rollback, no prediction:
every order waits three ticks, which feels like a slightly heavy mouse and
is normal for a tactics game. The slower side sets the pace, because the
faster one blocks on its frames.

**Questions this answers in an interview**
- Why must a lockstep protocol schedule orders ahead? (Otherwise a tick
  cannot run until the other side's orders arrive, so every tick waits a
  round trip.)
- How do you detect a desync in a live match? (Exchange state hashes with
  the frames and compare them for the ticks both sides have run.)
- What is signalling, and why can the game data skip the server afterwards?
