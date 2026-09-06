Vivid is one macOS app with two halves that share a single project: a **DAW-style Session View** for
sound, and a **rewireable visual node graph** for picture. A **mapping bridge** binds them, so what you
play becomes what you see. This page is the *how it works*, after the vision has landed - if you just
want to feel it, watch the [Gallery](/gallery/).

## Session View - the musical half

A familiar timeline of tracks, clips, scenes, instruments, effects and a mixer. Load Surge XT or any
VST3/CLAP plugin, write parts, arrange scenes. It is a real sequencer, not a toy - and every note,
every level, every transient it produces is available to drive the picture.

Bring music in the way you already work: **import a `.mid`** - the groove you dragged out of a drum
plugin's browser, or anything else on disk - or **play the part in from a MIDI keyboard**, sustain
pedal, mod wheel and pitch bend included. Controllers are recorded as editable automation on the clip,
not flattened away. When the arrangement is something you perform by hand, launching scenes as you go,
**record the master to a lossless WAV** while you play.

## The visual node graph - the picture half

A live graph of **visual operators**: real geometry, shaders, particles, compositing. You build it,
rewire it, and fork it like code while it runs. Operators are typed nodes with parameters and ports;
the edges are the signal path. Nothing here is a fixed pipeline - the graph *is* the instrument, and it
is yours to rearrange mid-performance.

## The mapping bridge - sound becomes picture

The bridge is what makes Vivid an instrument rather than two apps in a trench coat. Musical and control
signals (a bassline, a transient, an envelope, a clock) map onto visual parameters through typed
control edges. The bassline swells a metaball; a kick bursts a particle field; a bar-locked clock cuts
between scenes. It runs both ways: visual state can drive audio back.

## Author your own - code and AI over MCP

The built-in operators are teaching examples. The real product is the **plumbing**: when a look doesn't
exist yet, you write it. Author operators as project-local **WGSL shaders** or compiled **C++ nodes**,
edit them live, break them and recover using Vivid's own diagnostics.

And every operation in Vivid is exposed over **MCP** - so an AI agent is a first-class collaborator that
can author, inspect, modify and *verify* a whole project alongside you, not just nudge a knob. This is
the deepest part of the story; it is where "grasp the vision" becomes "make your own." Start in
[Learn](/learn/).

## Fork it - a project is a folder you own

A Vivid project is a portable folder: the session, the graph, the mappings, and your custom operators,
saved together. Take it, change it, run it anywhere. There is no lock-in and no cloud round-trip -
[download the app](https://github.com/seethroughlab/vivid/releases/latest) and open your first project.
