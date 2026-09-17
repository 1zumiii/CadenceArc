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

CadenceArc is `0.3.0-alpha` and experimental. The runtime API and asset format may change before the first stable release; see [Migration Notes](#migration-notes) for breaking changes in the development branch.

Available now:

- configurable action graphs backed by a `UDataAsset`;
- deterministic `InputTag -> ActionTag` transition resolution;
- explicit resolver states and a two-phase resolve/commit handshake;
- request IDs that correlate asynchronous execution callbacks;
- rejection, completion, cancellation, and interruption handling;
- a RequestId-protected input window with a single-slot, Last Input Wins buffer;
- caller-supplied timestamps and optional buffered-input expiry;
- structured, read-only result types with Blueprint accessors;
- shared graph validation for editor assets and resolver initialization;
- a physical press/release tracker (`FCadenceArcInputTracker`);
- memory-only Unreal Automation Tests.

In progress (Phase 6, Hold input): graphs can already describe input phases, held-duration ranges, and optional charge timing, and these are validated. The resolver still selects transitions by input tag only; duration-based selection, hold protection, and automatic release are not implemented yet. Do not rely on multi-tier Hold configuration at runtime.

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

`SubmitInput` validates the input tag and a finite, nonnegative timestamp before accepting an input. Zero is valid; invalid timestamps return `InvalidTimestamp` without replacing a valid buffer. For valid events the behavior depends on resolver state:

| Resolver state | Buffer window | Result |
| --- | --- | --- |
| `Ready` | Irrelevant | Resolves the graph immediately and may emit an `ActionRequest`. |
| `AwaitingStart` | Closed | Returns `NoAction / RequestPending` without changing state. |
| `Executing` | Open | Stores the input and returns `Buffered / None`; a later valid input overwrites it. |
| `Executing` | Closed | Returns `NoAction / BufferWindowClosed` without changing the stored input. |

The external executor controls the window with `OpenBufferWindow(RequestId)` and `CloseBufferWindow(RequestId)`. Both require the current executing request ID, so stale animation or state-machine notifications are harmless. Closing a window freezes the stored input rather than clearing it.

When the current action completes, `NotifyActionCompleted` returns an `FCadenceArcActionCompletionOutcome`:

- `GetHandshakeResult()` reports whether the callback matched the active request;
- `GetBufferConsumption()` and `GetBufferConsumptionReason()` report the resolution category and reason, meaningful only after a successful handshake;
- `GetNextActionRequest()` returns a copy of the next request when `HasNextActionRequest()` is true.

A consumed input moves the resolver directly to `AwaitingStart`. The next target action is still not committed until the executor reports `NotifyActionStarted`.

## Explicit Time and Expiry

The caller supplies time through both public APIs:

```cpp
FCadenceArcSubmitOutcome SubmitInput(
    const FCadenceArcInputEvent& InputEvent);

FCadenceArcActionCompletionOutcome NotifyActionCompleted(
    int64 RequestId,
    double CompletionTimestampSeconds);
```

`FCadenceArcInputEvent` carries `InputTag` and `double TimestampSeconds`, plus the Phase 6 fields `InputPhase` and `HeldDurationSeconds`. Input and completion timestamps must share one nondecreasing time domain, in seconds. The resolver never reads `UWorld`, platform time, or frame ticks; do not scale timestamps by frame rate or delta time. Choosing a consistent time domain (for example World game time, which pauses and follows time dilation) is the adapter's responsibility.

`UCadenceArcGraph::MaxBufferedInputAgeSeconds` defaults to `0.0`, which disables the age limit. Initialization rejects negative or non-finite limits as `InvalidGraph`. Last Input Wins replaces both tag and timestamp; closing the window preserves both.

After a successful completion handshake:

| Condition | Buffer result | Resulting state |
| --- | --- | --- |
| No buffered input | `NoAction / NoBufferedInput` | `Ready` |
| Completion time is non-finite, negative, or earlier than the buffered input | `Rejected / InvalidCompletionTime` | `Ready` |
| Age limit enabled and `CompletionTimestampSeconds - TimestampSeconds > MaxBufferedInputAgeSeconds` | `NoAction / Expired` | `Ready` |
| Within the limit, or limit disabled | Resolve the buffered input | `AwaitingStart` on success; otherwise `Ready` |

An age exactly equal to the limit is still valid. `InvalidCompletionTime` and `Expired` finish the old action, clear its request, window, and buffer, keep the committed node, and emit no next request. A failed handshake leaves all resolver state unchanged; its default consumption fields (`Rejected / None`) mean consumption was not attempted.

## Public Results

`FCadenceArcSubmitOutcome` exposes `GetCategory()`, `GetReason()`, `GetActionRequest()`, and `HasActionRequest()`. Fields are private; request getters return copies.

| Category | Meaning | Request |
| --- | --- | --- |
| `RequestProduced` | A candidate transition exists; `Reason` is `None`. | Candidate, awaiting Started |
| `Buffered` | The input replaced the single buffer slot; `Reason` is `None`. | Empty |
| `NoAction` | No transition now, e.g. `RequestPending`, `BufferWindowClosed`, `NoMatchingTransition`. | Empty |
| `Rejected` | Invalid call or graph data; inspect `Reason`. | Empty |

Default-constructed outcomes never indicate success, and empty requests have ID zero and empty tags. Branch on `HasActionRequest()` / `HasNextActionRequest()` rather than on request IDs; use `Reason` for diagnostics.

A minimal external executor:

```cpp
const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(InputEvent);
if (Submit.HasActionRequest())
{
    StartRequest(Submit.GetActionRequest());
}

const FCadenceArcActionCompletionOutcome Completion =
    Resolver->NotifyActionCompleted(CompletedRequestId, NowSeconds);
if (Completion.HasNextActionRequest())
{
    StartRequest(Completion.GetNextActionRequest());
}
```

`StartRequest` is the host's routine. It checks execution prerequisites, calls `NotifyActionRejected` when the action cannot run, and otherwise calls `NotifyActionStarted(RequestId)`. Execution begins only after that call returns `ECadenceArcHandshakeResult::Success`.

`UCadenceArcBlueprintLibrary` exposes the outcome getters and `Has*` checks as BlueprintPure nodes.

## Hold Input (Phase 6)

A **Hold** is one press-to-release sequence identified by an `FCadenceArcInputToken`. An immediate release is still a Hold; the name does not imply that a long-press threshold was reached. `FCadenceArcInputTracker` owns physical press/release pairing and measures held duration; the resolver will own the qualification to resolve that input.

- `ECadenceArcInputMode::PressOnly` submits on press. `HoldRelease` requests qualification on press and settles on manual or automatic release.
- `ECadenceArcHoldStage` is `None`, `Holding`, `Charging`, or `Charged`. Stages are derived from time, not stored. `Holding` means qualified but not charging, including holds without charge configuration.
- Transitions may set `InputPhase` and a half-open `DurationRange` (`[Min, Max)`, or unbounded). Ranges for the same source, tag, and phase must not overlap; adjacent ranges and gaps are allowed, so multiple release tiers can be expressed.
- `FCadenceArcHoldChargeConfig` in `FCadenceArcNode::HoldChargeConfigs` optionally adds charge protection and auto-release timing for one input tag. The full-charge threshold is the minimum of that tag's single unbounded Released range.

Planned resolver APIs: `BeginInputHold`, `ReleaseInputHold`, `CancelInputHold`, `AdvanceInputTime`, and `GetInputHoldSnapshot`.

## Editor Graph Validation

`UCadenceArcGraph::ValidateGraph` is shared by editor asset validation (`IsDataValid`, under `WITH_EDITOR`) and `UCadenceArcResolver::Initialize`. It reports empty graphs, invalid or duplicate node tags, invalid or missing entry nodes, invalid age limits, invalid transition tags, missing targets, invalid phases or ranges, overlapping transitions, and invalid hold charge configuration.

Validation never modifies the asset and emits diagnostics in deterministic array order. Initialization validates before replacing any state; failures return `InvalidGraph` and keep the previous configuration. Forward references, self-loops, cycles, terminal nodes, and reusing an input tag across nodes are allowed. Reachability analysis and edge priorities are not implemented. Resolution still checks current and target nodes at runtime, since graphs can change after initialization.

## Runtime Model

### Graph data

`FCadenceArcTransition`

- `InputTag`
- `TargetActionTag`
- `InputPhase`
- `bUseDurationRange` and `DurationRange`

`FCadenceArcNode`

- `ActionTag`
- `HoldChargeConfigs`
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

Request IDs are never reset by `Reset` or reinitialization, so stale asynchronous callbacks cannot match a newer request.

### Resolver states

| State | Meaning |
| --- | --- |
| `Uninitialized` | No valid graph is loaded. |
| `Ready` | A new input may be resolved. |
| `AwaitingStart` | A request exists and awaits acceptance or rejection. |
| `Executing` | The external executor confirmed that the requested action started. |

Only one outstanding request exists at a time.

### Lifecycle contract

| Event | Required state | Result |
| --- | --- | --- |
| `SubmitInput` resolves | `Ready` | Creates a request and enters `AwaitingStart`; current action is unchanged. |
| `NotifyActionStarted` | `AwaitingStart` | Commits the target action and enters `Executing`. |
| `NotifyActionRejected` | `AwaitingStart` | Returns to `Ready`, keeps the source action, and clears the request. |
| `NotifyActionCompleted` | `Executing` | Keeps the committed action, consumes the buffer, then enters `Ready` or emits the next request and enters `AwaitingStart`. |
| `NotifyActionCancelled` | `Executing` | Returns to `Ready`, resets to the entry action, and clears the request. |
| `NotifyActionInterrupted` | `Executing` | Returns to `Ready`, resets to the entry action, and clears the request. |

`Reset()` is only allowed in `Ready` and returns `Success`, `NotInitialized`, or `Busy`. Invalid request IDs, stale callbacks, and callbacks in the wrong state are rejected without mutating resolver state.

## Architectural Boundary

The core runtime module depends on Unreal Engine fundamentals and Gameplay Tags. It does not depend on:

- Gameplay Ability System;
- animation montages;
- a particular character or weapon class;
- collision or damage systems;
- any specific game project.

External systems consume `TargetActionTag` and report lifecycle events. GAS is one possible adapter, not a requirement.

## Repository Layout

```text
CadenceArc/
|-- CadenceArc.uplugin
|-- Config/
|-- Content/
|-- Resources/
`-- Source/
    `-- CadenceArc/
        |-- CadenceArc.Build.cs
        |-- Public/
        |   |-- Graph/
        |   |-- Input/
        |   `-- Resolver/
        `-- Private/
            |-- Graph/
            |-- Input/
            |-- Resolver/
            `-- Tests/
```

CadenceArc is developed and validated through the separate [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox) project, where this repository is mounted under `Plugins/CadenceArc` as a Git submodule.

## Testing

Tests live under `Source/CadenceArc/Private/Tests/` and build graphs in memory, without project assets:

- `CadenceArcTestSupport.h/.cpp` -- shared fixtures, tags, assertions, and time helpers;
- `Resolver/CadenceArcResolverContractTests.cpp` -- public outcomes, initialization, resolution, branches, and request IDs;
- `Resolver/CadenceArcResolverBufferTests.cpp` -- buffer windows, replacement, and consumption;
- `Resolver/CadenceArcResolverLifecycleTests.cpp` -- lifecycle callbacks, cancellation, interruption, and reset;
- `Resolver/CadenceArcResolverTimeTests.cpp` -- timestamps and buffer expiry;
- `Graph/CadenceArcGraphValidationTests.cpp` -- graph topology validation;
- `Graph/CadenceArcHoldValidationTests.cpp` -- phases, duration ranges, charge configuration, and naming redirects;
- `Input/CadenceArcInputTrackerTests.cpp` -- press/release pairing, duration, tokens, and cleanup.

Coverage focuses on contracts rather than happy paths: failure atomicity for initialization and handshakes, stale and out-of-order callbacks, Last Input Wins replacement, exact expiry boundaries, zero/negative/non-finite/backwards time, deterministic diagnostics, and validation non-mutation.

From a CadenceArcSandbox checkout, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

The runner performs a cold editor build and runs all `CadenceArc` tests with English Unreal output. Timing boundaries are verified with injected timestamps, not sleeps or manual frame timing.

## Migration Notes

Changes from earlier development versions of the API:

- `SubmitInput` returns `FCadenceArcSubmitOutcome` instead of a result enum plus an output request. Replace `ECadenceArcInputResult` comparisons with category/reason checks.
- Completion consumption is read through `GetBufferConsumption()` / `GetBufferConsumptionReason()`, gated on the handshake result. Old `Resolved` maps to `RequestProduced / None`, `InvalidTime` to `Rejected / InvalidCompletionTime`, and `Expired` to `NoAction / Expired`.
- `Reset()` returns `ECadenceArcResolverResetResult` instead of `bool`. The initialization result `Busy` is now `UnexpectedState`.
- Outcome fields are private; use the getters.
- Blueprint nodes using the old enum outputs must be reconnected manually; redirects cannot convert an enum output into an outcome struct.
- The former Gesture types were renamed to Hold without changing enum values: `ReleaseGestureConfig` -> `HoldChargeConfigs`, `PendingTap` -> `Holding`, `ReleaseGesture` -> `HoldRelease`. `Config/DefaultCadenceArc.ini` provides core redirects for existing assets. Test paths `CadenceArc.Graph.Gesture.*` are now `CadenceArc.Graph.Hold.*`.

Redirects are verified for loading and enum lookup; round-trip compatibility of every historical binary asset is not guaranteed.

## Roadmap

1. Finish Phase 6: resolver Hold qualification, duration-based selection, charge protection, and automatic release.
2. Phase 7: a read-only runtime graph debugger showing state, candidate and committed transitions, buffer windows, and diagnostic history.
3. Pause and directional input conditions, additional expiry policies, priorities, and reachability analysis.
4. Optional execution adapters, including GAS.
5. Input recording, replay, networking, and prediction research.

## Requirements

- Unreal Engine 5.7
- A supported Unreal Engine C++ toolchain
- Git LFS for binary Unreal assets
