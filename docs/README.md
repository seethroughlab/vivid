# Vivid 4 Documentation

This directory is organized by document lifecycle. The goal is to keep product intent, research,
experiments, decisions, and future implementation plans separate enough that each document can do one
job well.

## Start Here

- `../CLAUDE.md` - repo navigation for agents
- `../app/ARCHITECTURE.md` - current native architecture and thread model
- `decisions/ADR-0009-two-surface-bridge-and-cpp-poc.md` - pivot to two primary surfaces plus bridge
- `decisions/ADR-0010-poc-proven-production-seed.md` - C++ PoC promoted to product seed
- `decisions/ADR-0011-poc-to-product-architecture.md` - current trunk and selective-lift decision
- `roadmap/poc-to-product.md` - current productization roadmap
- `product/PRD.md` - product vision, scope, and current principles
- `product/glossary.md` - canonical vocabulary for Vivid 4 concepts
- `research/vivid-classic-lessons.md` - lessons from Vivid Classic and its commit history

Earlier Session View HTML prototypes in `experiments/` are historical pressure-test evidence. They
are not the current product direction unless a newer ADR or roadmap explicitly promotes them.

## Document Types

### Product

Product documents describe what Vivid 4 is for, who it serves, what concepts mean, and what
constraints shape the product. They should avoid implementation details unless a detail is part of
the product promise.

### Roadmap

Roadmap documents describe sequencing, status, gates, and progress. They should answer what gets
proved next, not how the system will be implemented.

### Research

Research documents preserve lessons, prior-art notes, retrospectives, and historical context. They
inform decisions but are not automatically binding requirements.

### Experiments

Experiment documents define prototypes, pressure tests, task proofs, evidence, failure modes, and
acceptance criteria. They are where ideas earn or lose confidence before becoming implementation
plans.

### Decisions

Decision records capture durable choices after they are accepted. Use an ADR when a decision will
shape architecture, product direction, vocabulary, or development process.

### Templates

Templates keep future phase plans, pressure tests, and decision records consistent without turning
the process into bureaucracy.

## Conventions

- Keep one document responsible for one job.
- Prefer links over duplicated explanations.
- Record decisions as ADRs once they stop being open questions.
- Define new product vocabulary in `product/glossary.md` before using it widely.
- Treat prototypes as evidence, not production architecture.
- Keep implementation plans separate from roadmap phases until a phase has passed its pressure test.
