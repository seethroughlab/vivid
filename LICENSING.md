# Licensing

## Quick Summary

| What | License | Owner |
|------|---------|-------|
| Vivid core (runtime, interface, seed operators, docs) | [Vivid Source Available License](LICENSE) | See-Through Lab LLC |
| Contributions to this repo (pull requests) | Assigned to See-Through Lab LLC via [CLA](CLA.md) | See-Through Lab LLC |
| Operator Packages (separate repos) | Your choice | You |
| Artworks you create with Vivid | Your choice | You |

## What You Can Do

**Use Vivid for your art.** Installations, performances, interactive
experiences, commercial exhibitions — all fine. The license explicitly
permits using Vivid to create audiovisual works for any purpose, including
commercial.

**Learn from the source.** Read, study, and reference the code.

**Build and sell operators.** Create Operator Packages in separate
repositories, license them however you want (MIT, GPL, proprietary,
commercial). They're your work. The only requirement is that they interface
through Vivid's public operator API and don't incorporate Vivid's core
source code.

**Teach with Vivid.** Faculty, students, and educational institutions can
use Vivid freely for teaching, coursework, and academic research.

**Contribute back.** Submit pull requests to improve Vivid. Contributions
to the core repo are assigned to See-Through Lab LLC via the CLA, which
ensures the project can be maintained, relicensed, and distributed
coherently. You retain a perpetual license to use your own contributions.

## What You Cannot Do

**Redistribute Vivid itself.** Don't repackage or distribute the runtime,
interface, or core source code.

**Build a competing tool.** Don't use Vivid's source code to create another
creative coding platform, node-based media tool, or similar product.

**Offer Vivid as a service.** Don't host Vivid as SaaS or a cloud service.

## The Core / Operator Split

Vivid has a deliberate architectural boundary between the **core** (this
repository) and **Operator Packages** (separate repositories like
`vivid-glitch`, `vivid-wavetable`, `vivid-3d`).

The core provides the environment: the runtime, graph engine, scheduler, UI,
operator API contract, and a full set of built-in operators covering drums,
samplers, sequencers, synthesis, effects, and GPU visuals. It's owned by
See-Through Lab LLC under the Vivid Source Available License.

Operator Packages are self-contained extensions that plug into the core
through the public operator API. They are **not** covered by this license.
You own your operators and can license them however you choose.

This split is intentional. The core ships enough to be immediately useful;
packages extend it with specialized or opinionated functionality. The
environment is proprietary; the ecosystem on top of it is open.

## Future Open Source

See-Through Lab LLC may choose to release Vivid under an open source
license in the future. This is not guaranteed or scheduled. Any such change
would apply going forward and would not affect rights already granted.

## Questions

jeff@see-through.studio
