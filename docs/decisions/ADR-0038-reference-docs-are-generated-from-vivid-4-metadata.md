# ADR-0038: Reference Docs Are Generated From Vivid 4 Metadata

Status: accepted

Date: 2026-07-26

## Context

Classic's site generated operator and package reference pages from JSON metadata, manifests, source
comments, and package docs. Vivid 4 already has metadata sources worth publishing: operator
descriptors, package manifests, shader JSON headers, MCP catalog responses, and project-local
package information.

Hand-written reference pages would drift quickly, especially while shader/operator metadata is still
evolving.

## Decision

The revived website will generate Vivid 4 reference docs from Vivid 4 metadata, not from copied
classic data or hand-maintained pages.

The initial generated reference scope is:

- Shader/operator reference from current registered operator metadata and shader headers.
- Package/local operator metadata from `vivid-package.json` manifests where available.
- Status notes when metadata is incomplete, invalid, or intentionally hidden.

The first generator may be simpler than classic's, but its source of truth must be current Vivid 4
metadata. Missing metadata should be treated as a product/docs gap and fixed at the operator or
manifest level where possible.

## Alternatives Considered

- **Copy classic operator docs as a starting point.** Rejected. The operator set and product model
  have diverged.
- **Hand-write a small operator list.** Rejected except as temporary placeholder copy. It will drift.
- **Defer all reference docs until metadata is perfect.** Rejected. Generating imperfect docs early
  exposes metadata gaps and improves the product catalog.

## Consequences

- Reference work depends on catalog/introspection quality.
- Operator authors need to care about descriptor text, parameter names, choices, semantic tags, and
  shader headers.
- The website build may need a headless app/catalog export step, or a checked-in generated metadata
  snapshot with a validation gate.

