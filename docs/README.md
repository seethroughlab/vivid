# Vivid 4 Documentation

This directory is organized by document lifecycle. The goal is to keep product intent, research,
experiments, decisions, and future implementation plans separate enough that each document can do one
job well.

## Start Here

- `product/PRD.md` - product vision, scope, principles, and first proof target
- `product/glossary.md` - canonical vocabulary for Vivid 4 concepts
- `roadmap/phased-development-plan.md` - high-level phase ledger and progress tracking
- `research/vivid-classic-lessons.md` - lessons from Vivid Classic and its commit history
- `experiments/session-view-pressure-test.md` - first Session View pressure test
- `experiments/session-view-pressure-test.html` - disposable clickable mock

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
