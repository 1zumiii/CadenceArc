# CadenceArc

CadenceArc is a tag-driven, execution-agnostic branching action framework for Unreal Engine 5.

It resolves semantic input tags through a configurable action graph and emits action requests without knowing how those actions are executed.

```text
InputEvent (InputTag + TimestampSeconds)
  -> SubmitInput
  -> resolve now or buffer during execution
  -> ActionRequest
  -> external executor
  -> lifecycle handshake
  -> consume buffered input on completion
```

The name reflects the long-term design: player **cadence** shapes an **arc** through a branching sequence of actions.

## Status

CadenceArc is currently at `0.3.0-alpha`. Its runtime API and asset format may change before the first stable release.

The current development checkout completes Phase 5 (explicit time and optional buffered-input expiry), followed by editor graph validation. The descriptor version above has not been bumped as part of this documentation update. The Phase 5 input and completion signatures are breaking changes from the original `0.3.0-alpha` API.

The current milestone provides:

- configurable action graphs backed by a `UDataAsset`;
- deterministic `InputTag -> ActionTag` transition resolution;
- explicit resolver states;
- request IDs that correlate asynchronous execution callbacks;
- two-phase resolution and execution commit;
- rejection, completion, cancellation, and interruption handling;
- a RequestId-protected input window;
- a single-slot, Last Input Wins input buffer;
- automatic buffered-input resolution when an action completes;
- structured completion outcomes containing handshake and buffer-consumption results;
- caller-supplied timestamps and optional graph-wide buffered-input expiry;
- editor asset validation with deterministic, actionable diagnostics;
- Blueprint-accessible data and resolver APIs;
- memory-only Unreal Automation Tests.

Hold and release phases, cadence conditions, production execution adapters, and networking are not implemented yet. A temporary Enhanced Input and Timer adapter lives in CadenceArcSandbox.

## Why the Handshake Exists

Finding a graph transition does not guarantee that an external action can start. A Gameplay Ability, character state machine, or AI executor may reject a request because of resource, state, or timing constraints.

CadenceArc therefore separates resolution from commitment:

```text
Resolve input
  -> produce ActionRequest
  -> executor accepts or rejects
  -> Started commits the target node
  -> terminal callback closes the request
```

The resolver never assumes that an emitted action was successfully executed.

## Input Buffering

`SubmitInput` validates the input tag and a finite, nonnegative timestamp before accepting an input. Zero is valid; invalid timestamps return `InvalidTimestamp` without replacing a valid buffer. For valid events it has state-dependent behavior:

| Resolver state | Buffer window | Result |
| --- | --- | --- |
| `Ready` | Irrelevant | Resolves the graph immediately and may emit an `ActionRequest`. |
| `AwaitingStart` | Closed | Returns `RequestPending` without changing state. |
| `Executing` | Open | Stores the input and returns `Buffered`; a later valid input overwrites it. |
| `Executing` | Closed | Returns `BufferWindowClosed` without changing the stored input. |

The external executor controls the timing window with `OpenBufferWindow(RequestId)` and `CloseBufferWindow(RequestId)`. Both calls require the current executing request ID, making stale animation or state-machine notifications harmless. Closing a window freezes the stored input rather than clearing it.

When the current action completes, `NotifyActionCompleted` returns an `FCadenceArcActionCompletionOutcome`:

- `HandshakeResult` reports whether the completion callback matched the active request;
- `BufferConsumeResult` reports whether a buffered input was absent, resolved, expired, had invalid time, or failed graph resolution;
- `NextActionRequest` contains the next request when consumption succeeds.

A successfully consumed input moves the resolver directly to `AwaitingStart`. The next target action is still not committed until the external executor reports `NotifyActionStarted`.

## Explicit Time and Expiry

The caller supplies time through both public APIs:

```cpp
ECadenceArcInputResult SubmitInput(
    const FCadenceArcInputEvent& InputEvent,
    FCadenceArcActionRequest& OutActionRequest);

FCadenceArcActionCompletionOutcome NotifyActionCompleted(
    int64 RequestId,
    double CompletionTimestampSeconds);
```

`FCadenceArcInputEvent` contains `InputTag` and `double TimestampSeconds`. Input and completion timestamps must use the same nondecreasing time domain, measured in seconds. The resolver does not read `UWorld`, platform time, or frame ticks, and timestamps must not be multiplied by frame rate or delta time. It validates the completion-to-buffer relationship; it does not maintain a global clock or validate the ordering of every submitted event.

Sandbox supplies World game time; memory-only tests supply fixed values. Game time pauses with the world and follows time dilation. Selecting another consistent time domain is the adapter's responsibility.

`UCadenceArcGraph::MaxBufferedInputAgeSeconds` defaults to `0.0`, which disables the age limit. Initialization rejects negative or non-finite limits as `InvalidGraph`, without replacing an existing valid configuration. Last Input Wins replaces both the tag and timestamp; closing the window preserves both.

After a successful completion handshake:

| Condition | Buffer result | Resulting state |
| --- | --- | --- |
| No buffered input | `NoBufferedInput`; completion time is irrelevant | `Ready` |
| Completion time is non-finite, negative, or earlier than the buffered input | `InvalidTime` | `Ready` |
| Age limit is enabled and `CompletionTimestampSeconds - TimestampSeconds > MaxBufferedInputAgeSeconds` | `Expired` | `Ready` |
| Input is within the limit, or the limit is disabled | Resolve the buffered tag | `AwaitingStart` on success; otherwise `Ready` |

An age exactly equal to the limit remains valid. Disabling expiry skips only the age-limit comparison; timestamp validation still applies. `InvalidTime` and `Expired` finish the old action, clear its request, window, and buffer, and preserve the committed node. They emit no next request and keep `HandshakeResult = Success`. A failed handshake leaves all resolver state unchanged and reports `NotAttempted` for buffer consumption.

## Editor Graph Validation

`UCadenceArcGraph::IsDataValid` integrates with Unreal's asset validation under `WITH_EDITOR`. It reports empty graphs, invalid or duplicate node tags, invalid or missing entry nodes, invalid age limits, invalid transition tags, missing targets, and duplicate input tags within a source node.

Validation collects diagnostics without modifying the asset and emits them in deterministic array order. A valid graph returns `Valid`, while errors return `Invalid`. Forward references, self-loops, cycles, terminal nodes, and reuse of an input tag across different nodes are allowed. Reachability analysis and conditional-edge priorities are not implemented.

Editor validation does not replace runtime guards. Runtime initialization checks the graph, age limit, and entry node; resolution still checks the current and target nodes. No `UnrealEd` dependency is added to the runtime module.

## Runtime Model

### Graph data

`FCadenceArcTransition`

- `InputTag`
- `TargetActionTag`

`FCadenceArcNode`

- `ActionTag`
- `Transitions`

`UCadenceArcGraph`

- `EntryActionTag`
- `Nodes`
- `MaxBufferedInputAgeSeconds`

The entry node may use a non-executable root tag that only represents the initial resolver state.

### Action request

`FCadenceArcActionRequest` contains:

- `RequestId` -- a positive, monotonically increasing identifier;
- `InputTag` -- the semantic input that selected the transition;
- `SourceActionTag` -- the current node when resolution occurred;
- `TargetActionTag` -- the candidate action selected by the graph.

Request IDs are not reset by `Reset` or reinitialization, preventing stale asynchronous callbacks from matching a newer request.

### Resolver states

| State | Meaning |
| --- | --- |
| `Uninitialized` | No valid graph is loaded. |
| `Ready` | A new input may be resolved. |
| `AwaitingStart` | A request exists and awaits acceptance or rejection. |
| `Executing` | The external executor confirmed that the requested action started. |

Only one outstanding request exists at a time in the current model.

### Lifecycle contract

| Event | Required state | Result |
| --- | --- | --- |
| `SubmitInput` resolves | `Ready` | Creates a request and enters `AwaitingStart`; current action is unchanged. |
| `NotifyActionStarted` | `AwaitingStart` | Commits the target action and enters `Executing`. |
| `NotifyActionRejected` | `AwaitingStart` | Returns to `Ready`, preserves the source action, and clears the request. |
| `NotifyActionCompleted` | `Executing` | Preserves the committed action, consumes the buffer, then enters `Ready` or emits the next request and enters `AwaitingStart`. |
| `NotifyActionCancelled` | `Executing` | Returns to `Ready`, resets to the entry action, and clears the request. |
| `NotifyActionInterrupted` | `Executing` | Returns to `Ready`, resets to the entry action, and clears the request. |

Invalid request IDs, stale callbacks, and callbacks received in the wrong state are rejected without mutating resolver state.

## Architectural Boundary

The core runtime module depends on Unreal Engine fundamentals and Gameplay Tags. It does not depend on:

- Gameplay Ability System;
- animation montages;
- a particular character or weapon class;
- collision or damage systems;
- the WarriorRPG project.

External systems consume `TargetActionTag` and report lifecycle events. GAS is one possible adapter, not a requirement of the core framework.

## Repository Layout

```text
CadenceArc/
|-- CadenceArc.uplugin
|-- Content/
|-- Resources/
`-- Source/
    `-- CadenceArc/
        |-- CadenceArc.Build.cs
        |-- Public/
        |   |-- Graph/
        |   `-- Resolver/
        `-- Private/
            |-- Graph/
            |-- Resolver/
            `-- Tests/
```

CadenceArc is developed and validated through the separate [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox) project, where this repository is mounted under `Plugins/CadenceArc` as a Git submodule.

## Testing

The editor suite contains 31 Unreal Automation Tests: 25 resolver tests and 6 editor graph-validation groups. The tests are organized under `Source/CadenceArc/Private/Tests/`:

- `CadenceArcTestSupport.h/.cpp`: shared graph fixtures, tags, assertions, and time helpers;
- `Resolver/CadenceArcResolverContractTests.cpp`: public outcomes, initialization, resolution, branches, and request IDs;
- `Resolver/CadenceArcResolverBufferTests.cpp`: buffer windows, replacement, and consumption;
- `Resolver/CadenceArcResolverLifecycleTests.cpp`: lifecycle callbacks, cancellation, interruption, and reset;
- `Resolver/CadenceArcResolverTimeTests.cpp`: timestamps and buffer expiry;
- `Graph/CadenceArcGraphValidationTests.cpp`: editor graph validation.

Coverage includes:

- graph initialization and failure atomicity;
- public outcome categories and reasons, default values, request availability, and request-getter copy isolation;
- action request creation;
- valid light, heavy, and finisher branches;
- pending and executing state guards;
- every lifecycle callback;
- cancellation and interruption recovery;
- invalid, stale, and out-of-order callbacks;
- buffer-window RequestId validation and idempotent open/close behavior;
- single-slot buffering and Last Input Wins replacement;
- buffered completion producing the correct next action request;
- no-match and broken-graph failures during buffer consumption;
- buffer cleanup after completion, cancellation, and interruption;
- reset and reinitialization rules;
- monotonically increasing request IDs;
- zero, negative, non-finite, and backwards time;
- disabled expiry, exact age boundaries, and full event replacement;
- completion recovery and handshake precedence over invalid time;
- valid graph topology and invalid node, edge, entry, and age configuration;
- multiple diagnostics, repeatable diagnostic ordering, and validation non-mutation.

From a CadenceArcSandbox checkout, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

The runner performs a cold editor build and then runs tests with English Unreal output to avoid localized result-parsing issues in Rider.

Exact timing boundaries are verified with injected timestamps, without sleeps or manual frame timing. PIE smoke checks cover real input integration and observable execution; they do not require a person to distinguish subsecond boundaries.

## Roadmap

Planned work includes:

1. press, release, hold, pause, and directional conditions;
2. node- or transition-level expiry policies;
3. transition conditions, priority, and ambiguity validation;
4. graph reachability analysis and richer debugging tools;
5. optional execution adapters, including GAS;
6. input recording, replay, networking, and prediction research.

A future API design review will consider separating a small set of caller-facing outcomes from detailed diagnostic reasons. This is a proposal; the current enums and the separate handshake/consumption outcomes remain in place.

## Requirements

- Unreal Engine 5.7
- A supported Unreal Engine C++ toolchain
- Git LFS for binary Unreal assets
