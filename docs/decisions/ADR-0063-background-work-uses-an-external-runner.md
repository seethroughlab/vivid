# ADR-0063: Background Work Is Resumable and Uses an External Runner

Status: proposed — worker isolation and recovery proofs pending

Date: 2026-09-17

Follows: [ADR-0061](ADR-0061-creative-work-persists-across-refinement-rounds.md) and
[ADR-0062](ADR-0062-candidates-are-isolated-and-promoted-explicitly.md).
Refines: [ADR-0006](ADR-0006-agent-external-mcp.md),
[ADR-0008](ADR-0008-agent-capability-surface.md), and
[ADR-0032](ADR-0032-audio-io-latency-and-export-are-product-surfaces.md).

## Context

MCP exposes operations but does not by itself schedule an agent or keep it running. Vivid currently
has one shared App/document per process. Its synchronous audio export and asynchronous AV export
both use the active session; additional windows share that state. These facilities are foundations,
not an isolated, durable background execution system.

## Decision

1. **Keep reasoning and scheduling external.** A swappable runner claims work, reasons, schedules
   attempts, enforces provider budgets, and reports outcomes. Vivid owns durable work records,
   candidate state, constraints, validation, and deterministic operations. The MCP bridge exposes
   this contract; it is not the authoritative job store or a hidden scheduler.
2. **Execute candidates in a separate local worker process initially.** The worker loads an isolated
   candidate and performs edits, rendering, and evaluation without changing the foreground session.
   Start with one worker and one project. Validate GPU, plugin, audio device, file-path, port, and
   recovery behavior before claiming background compatibility. Foreground playback takes priority;
   resource limits may throttle or pause worker activity.
   The worker shares no per-user mutable state with the foreground. Known shared state in the trunk
   that must be separated or disabled for a worker: the control-server port (`VIVID_PORT`), the
   single autosave/recovery slot and the crash-history and quarantine records under the user data
   directory, and the launch-time recovery prompt (`VIVID_NO_RECOVER`). Project-local operators
   compile into the project folder on load; a candidate copy therefore compiles into its own folder,
   at the cost of a compile per worker load, and those build outputs are not part of the version
   snapshot.
3. **Expose durable asynchronous jobs.** Submissions return stable IDs promptly. Status and results
   identify exact input versions and output artifacts. Jobs distinguish queued, running, completed,
   failed, canceled, and interrupted states. A work item waiting for review is distinct from a
   completed execution job. Publish review readiness only after artifacts are complete and accessible.
4. **Make reconnection safe.** Submission/feedback deduplication keys prevent duplicate effects.
   Renewable work leases identify the current runner; lease generations fence stale owners from
   further authoritative writes after reassignment. On restart, reconcile interrupted work against
   durable checkpoints. Restarting a render from the beginning is acceptable; sample-level resume
   is not required. Do not promise exactly-once provider execution after an uncertain network result.
5. **Make cancellation and revision changes explicit.** Pause stops scheduling new work and reports
   whether an active operation is still stopping. Cancel requests safe termination while preserving
   completed artifacts. Recheck brief/protection revisions before further mutations and publication;
   superseded results retain provenance but cannot be promoted under stale constraints.
6. **Expose compact updates and honest availability.** Clients can read changes after a cursor;
   expired cursors have a full-resync path. Capability discovery distinguishes unavailable operations
   and workers. “Queued; no runner connected” is not “working.” Disconnected runners become
   interrupted/unknown after lease expiry. Provider cost is reported as unknown when unobservable.

The MCP surface should add work, version, review, execution, and update families above existing
editing tools. Candidate selection must be explicit or bound unambiguously to an isolated worker;
it must never fall back to the foreground project. Endpoint names and storage layouts are deferred.

## Alternatives Considered

- **Embed a permanent model loop in Vivid.** Couples the application to provider and scheduling churn.
- **Treat a long-lived MCP connection as durable work.** Connection lifetime does not preserve intent,
  job results, or ownership after restart.
- **Use another window of the same App.** Shares the document and engine, providing no isolation.
- **Immediately build a hosted worker fleet.** Adds asset, plugin, identity, and operational scope
  before proving the local creative loop.

## Consequences

Worker supervision, local resource contention, migrations, and recovery become explicit engineering
work. The deterministic offline AV export is already a surface-free, resumable render of the visual
graph at a synthetic clock and is the worker's render primitive; it is not rebuilt. What the worker
adds is device-free process bring-up: a GPU context created without a window surface, no audio
device opened (the foreground keeps the hardware; the sample rate is a job input), and the durable
job/lease contract above. Wrapping the existing endpoints with a job ID alone provides neither
isolation nor durability. Provider integrations remain replaceable.

## Open Availability Boundary

The proposed first proof uses an awake Mac. Whether the product must continue executing while that
Mac sleeps is unresolved, not an accepted limitation. That requirement would need another execution
host. Define behavior separately for closing Review, quitting Vivid, runner exit, and system sleep;
do not label any stopped execution as active. No automation is created by this ADR.

## Acceptance Evidence Required

Render a candidate during foreground playback/editing from a worker that has opened neither a window
surface nor an audio device. Exercise cancel, worker crash, runner restart,
lease expiry/reassignment, stale-runner writes, duplicate submissions, and sleep/wake. Demonstrate
recovery without false completion, duplicate promotion, or foreground corruption, and prove a worker
crash or autosave never populates the foreground's recovery prompt. Evidence is pending.
