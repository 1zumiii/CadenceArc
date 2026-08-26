# CadenceArc

CadenceArc is a tag-driven, execution-agnostic branching action framework for Unreal Engine 5.

It resolves semantic input tags through a configurable action graph and emits action requests without knowing how those actions are executed.

```text
InputTag
  -> action graph resolution
  -> ActionRequest
  -> external executor
  -> lifecycle handshake
```

The name reflects the long-term design: player **cadence** shapes an **arc** through a branching sequence of actions.

## Status

CadenceArc is currently at `0.2.0-alpha`. Its runtime API and asset format may change before the first stable release.

The current milestone provides:

- configurable action graphs backed by a `UDataAsset`;
- deterministic `InputTag -> ActionTag` transition resolution;
- explicit resolver states;
- request IDs that correlate asynchronous execution callbacks;
- two-phase resolution and execution commit;
- rejection, completion, cancellation, and interruption handling;
- Blueprint-accessible data and resolver APIs;
- memory-only Unreal Automation Tests.

Input buffering, timing windows, cadence conditions, execution adapters, and networking are not implemented yet.

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
| Resolve succeeds | `Ready` | Creates a request and enters `AwaitingStart`; current action is unchanged. |
| `NotifyActionStarted` | `AwaitingStart` | Commits the target action and enters `Executing`. |
| `NotifyActionRejected` | `AwaitingStart` | Returns to `Ready`, preserves the source action, and clears the request. |
| `NotifyActionCompleted` | `Executing` | Returns to `Ready`, preserves the committed action, and clears the request. |
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

The current suite contains 11 Unreal Automation Tests covering:

- graph initialization and failure atomicity;
- action request creation;
- valid light, heavy, and finisher branches;
- pending and executing state guards;
- every lifecycle callback;
- cancellation and interruption recovery;
- invalid, stale, and out-of-order callbacks;
- reset and reinitialization rules;
- monotonically increasing request IDs.

From a CadenceArcSandbox checkout, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

The runner performs a cold editor build and then runs tests with English Unreal output to avoid localized result-parsing issues in Rider.

## Roadmap

Planned work includes:

1. input buffering and externally controlled timing windows;
2. press, release, hold, pause, and directional conditions;
3. transition conditions, priority, and ambiguity validation;
4. graph data validation and debugging tools;
5. optional execution adapters, including GAS;
6. input recording, replay, networking, and prediction research.

## Requirements

- Unreal Engine 5.7
- A supported Unreal Engine C++ toolchain
- Git LFS for binary Unreal assets

