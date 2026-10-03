# CadenceArc

[简体中文](README.md) | English

CadenceArc is a data-driven combo resolution framework for Unreal Engine 5. Combo rules are configured as an action graph in a DataAsset, and inputs and actions are identified by semantic GameplayTags. The host submits input events and action lifecycle callbacks to the resolver, which produces action requests from the graph and hands them to an external execution system. The framework does not depend on any particular execution method, such as GAS or animation montages.

![Arc Debugger during a PIE combo: conditional edges, the input display, and Arc History](Docs/Images/arc-debugger-overview.png)

*The runtime debugger in PIE: the current action is green, the moves it can branch into next stay bright, and every resolver call is listed on the right.*

## What It Does

```text
InputEvent (InputTag + timestamp)
  -> resolver: resolve now, or buffer while an action runs
  -> ActionRequest
  -> your executor (GAS, montages, a state machine, ...)
  -> handshake callbacks: started, completed, rejected, interrupted
```

CadenceArc only decides which move comes next. How an action plays, deals damage, or animates stays outside the framework. The core module depends only on Engine and Gameplay Tags, not on GAS, animation, or any particular game.

## Features

- **Action graphs**: a `UDataAsset` keyed by Gameplay Tags, validated in the editor.
- **Standard integration component**: `UCadenceArcComponent` handles timestamps, per-frame advance, press/release pairing, input modes, and a single request outlet, and reports input results and hold endings. You configure input modes and the context source once and implement the executor callbacks.
- **Two-phase handshake**: a resolved request commits only when the executor confirms it started; a rejection leaves state unchanged.
- **Input buffering**: an executor-controlled window with a single slot (last input wins) and optional expiry.
- **Hold and charge**: different moves for different hold durations, with charge stages, charge protection, and automatic release.
- **Transition conditions**: the same input can branch on context tags (per input or persistent) and on the pause since the last action, with explicit priorities. Ties are reported, never guessed.
- **Explicit time**: the caller supplies every timestamp; the resolver never reads a clock, so results are deterministic.
- **Runtime debugger**: editor-only live graph view, an input display, and a call history that explains failures, including which condition failed.

## Quick Start

1. Put this repository at `Plugins/CadenceArc` in your project (a Git submodule works) and enable the plugin.
2. Add `"CadenceArc"` and `"GameplayTags"` to your module's `Build.cs` dependencies.
3. Create a `CadenceArcGraph` data asset with an entry node, nodes, and transitions.
4. Add a `UCadenceArcComponent` to your character, assign the graph to its `Graph` property, and set `HoldRelease` in `InputModes` for inputs that wait for release. The component handles timestamps, per-frame advance, press/release pairing, and the request outlet. If you need event context such as direction, implement `ICadenceArcInputContextProvider` on the character.
5. Call `PressInput` and `ReleaseInput` from your input bindings. Your executor subscribes to `OnActionRequested` and reports the action lifecycle back:

```cpp
// Input binding: the mode comes from InputModes, event context from the character's CollectInputContext
CadenceArcComponent->PressInput(InputTag);
CadenceArcComponent->ReleaseInput(InputTag);

// Executor: every action request arrives here, including the one produced when a buffered input is consumed
void UMyExecutor::HandleActionRequested(const FCadenceArcActionRequest& Request)
{
    if (!CanPlay(Request.TargetActionTag))
    {
        CadenceArcComponent->NotifyActionRejected(Request.RequestId);
        return;
    }
    CadenceArcComponent->NotifyActionStarted(Request.RequestId);
    PlayAction(Request); // call CadenceArcComponent->NotifyActionCompleted(Request.RequestId) when it ends
}
```

See [CadenceArc component](Docs/Component.md) for the full interface. Tests, replays, and other cases that need direct control over time can use `UCadenceArcResolver` without the component.

A complete, playable example lives in [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox): it drives CadenceArc with Enhanced Input and a Timer-based demo executor, including hold input and transition conditions.

## Documentation

The detailed docs are written in Chinese.

| Document | Covers |
| --- | --- |
| [CadenceArc component](Docs/Component.md) | The standard integration: what the component handles, setup, interface, time source |
| [Resolver: handshake, buffering, and time](Docs/Resolver.md) | States and lifecycle, executor integration, result types, buffer windows, time and expiry, context and pause |
| [Hold input](Docs/HoldInput.md) | Release tiers, charge configuration, per-frame advance, host integration notes |
| [Action graph and validation](Docs/Graph.md) | Graph fields, transition conditions and priorities, validation rules |
| [Runtime debugger](Docs/Debugger.md) | Arc Debugger, condition labels, input display, Arc History, layout options, Sandbox debug scenarios |
| [Testing](Docs/Testing.md) | How to run the tests and what each file covers |
| [Migration notes](Docs/Migration.md) | Breaking changes since earlier development versions |

## Status

CadenceArc is `0.4.0-alpha` and experimental; the API and asset format may change before the first stable release.

- Done: core resolution and handshake, buffering and expiry, hold and charge (Phase 6), runtime debugger (Phase 7), transition conditions and priorities (Phase 8).
- The hold API has not been used in a shipped game yet, so its ergonomics may still change.
- The Sandbox uses a Timer-based demo executor; CadenceArc has not yet been tested with an animation montage executor.

Roadmap:

1. More expiry policies, and an optional combo timeout that returns to the entry.
2. Optional execution adapters, including GAS.
3. Input recording, replay, networking, and prediction research.

## Requirements

- Unreal Engine 5.7 and a supported C++ toolchain
- Git LFS for binary Unreal assets
