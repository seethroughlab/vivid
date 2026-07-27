# ADR-0039: Community Packages Are Coming Soon Until Registry Trust Exists

Status: accepted

Date: 2026-07-26

## Context

Classic had a broader package ecosystem and website catalog. Vivid 4 currently has a right-sized
local package system: manifests, local install/build/reload, project-local packages, shader-as-op
metadata, MCP authoring tools, catalog discovery, and crash quarantine. That is enough for local and
project-scoped creative coding, but not enough for a public community package registry.

A public registry adds distribution, trust, compatibility, moderation, and update responsibilities.
Those should not be implied by a premature website catalog.

## Decision

The revived website will present community packages as **Coming soon** until Vivid has an explicit
remote registry and trust model.

The implementation order is:

1. Keep local/project package authoring working and documented.
2. Generate local/package reference docs from manifests and metadata as described in ADR-0038.
3. Define a remote registry format and hosting location.
4. Add provenance policy: checksums, signing or trusted sources, and compatibility metadata.
5. Add install/update UX for remote packages.
6. Add package CI that builds examples against current Vivid.
7. Add catalog states: curated, experimental, deprecated, broken, removed.
8. Publish the community catalog only after those controls exist.

## Alternatives Considered

- **Revive the classic catalog immediately.** Rejected. It would imply distribution guarantees that
  Vivid 4 does not yet provide.
- **Never host community packages.** Rejected as a final answer. Packages are core to creative
  coding extensibility.
- **Link to arbitrary third-party repos manually.** Rejected for the main site. Untrusted links can
  exist in docs or examples, but not as an implied installable catalog.

## Consequences

- The website can ship before the community registry, but must label the package story honestly.
- Registry work becomes a later ADR/implementation chain, not a hidden dependency of the first
  placeholder page.
- Local/project packages remain the immediate creative coding path.

