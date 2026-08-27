# CadenceArc

CadenceArc is a tag-driven, execution-agnostic branching action framework for Unreal Engine 5.

It resolves semantic input tags through a configurable action graph and emits action requests without knowing how those actions are executed.

```text
InputTag
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
- Blueprint-accessible data and resolver APIs;
- memory-only Unreal Automation Tests.

Input timestamps, hold and release phases, cadence conditions, execution adapters, and networking are not implemented yet.

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

`SubmitInput` has state-dependent behavior:

| Resolver state | Buffer window | Result |
| --- | --- | --- |
| `Ready` | Irrelevant | Resolves the graph immediately and may emit an `ActionRequest`. |
| `AwaitingStart` | Closed | Returns `RequestPending` without changing state. |
| `Executing` | Open | Stores the input and returns `Buffered`; a later valid input overwrites it. |
| `Executing` | Closed | Returns `BufferWindowClosed` without changing the stored input. |

The external executor controls the timing window with `OpenBufferWindow(RequestId)` and `CloseBufferWindow(RequestId)`. Both calls require the current executing request ID, making stale animation or state-machine notifications harmless. Closing a window freezes the stored input rather than clearing it.

When the current action completes, `NotifyActionCompleted` returns an `FCadenceArcActionCompletionOutcome`:

- `HandshakeResult` reports whether the completion callback matched the active request;
- `BufferConsumeResult` reports whether a buffered input was absent, resolved, or failed graph validation;
- `NextActionRequest` contains the next request when consumption succeeds.

A successfully consumed input moves the resolver directly to `AwaitingStart`. The next target action is still not committed until the external executor reports `NotifyActionStarted`.

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

The current suite contains 15 Unreal Automation Tests covering:

- graph initialization and failure atomicity;
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
- monotonically increasing request IDs.

From a CadenceArcSandbox checkout, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

The runner performs a cold editor build and then runs tests with English Unreal output to avoid localized result-parsing issues in Rider.

## Roadmap

Planned work includes:

1. press, release, hold, pause, and directional conditions;
2. injectable time semantics and input expiry;
3. transition conditions, priority, and ambiguity validation;
4. graph data validation and debugging tools;
5. optional execution adapters, including GAS;
6. input recording, replay, networking, and prediction research.

## Requirements

- Unreal Engine 5.7
- A supported Unreal Engine C++ toolchain
- Git LFS for binary Unreal assets
