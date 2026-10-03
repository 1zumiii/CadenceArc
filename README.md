# CadenceArc

English | [简体中文](README.zh-CN.md)

CadenceArc is a branching combo framework for Unreal Engine 5. You describe "in this action, on this input, go to that action" in an action graph; CadenceArc turns inputs into action requests and hands them to your own execution system.

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
- **Two-phase handshake**: a resolved request commits only when the executor confirms it started; a rejection leaves state unchanged.
- **Input buffering**: an executor-controlled window with a single slot (last input wins) and optional expiry.
- **Hold and charge**: different moves for different hold durations, with charge stages, charge protection, and automatic release.
- **Transition conditions**: the same input can branch on context tags (per input or persistent) and on the pause since the last action, with explicit priorities. Ties are reported, never guessed.
- **Explicit time**: the caller supplies every timestamp; the resolver never reads a clock, so results are deterministic.
- **Runtime debugger**: editor-only live graph view, an input display, and a call history that explains failures, including which condition failed.
- **Automated tests**: 130 Unreal automation tests covering time boundaries, stale callbacks, and failure atomicity.

## Quick Start

1. Put this repository at `Plugins/CadenceArc` in your project (a Git submodule works) and enable the plugin.
2. Add `"CadenceArc"` and `"GameplayTags"` to your module's `Build.cs` dependencies.
3. Create a `CadenceArcGraph` data asset with an entry node, nodes, and transitions.
4. Create a resolver in your executor and wire input and lifecycle callbacks into it:

```cpp
Resolver = NewObject<UCadenceArcResolver>(this);
Resolver->Initialize(ComboGraph);

// Input: may produce a request right away
const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(InputEvent);
if (Submit.HasActionRequest())
{
    StartRequest(Submit.GetActionRequest()); // NotifyActionStarted if it can run, otherwise NotifyActionRejected
}

// Action finished: a buffered input may produce the next request
const FCadenceArcActionCompletionOutcome Completion = Resolver->NotifyActionCompleted(RequestId, NowSeconds);
if (Completion.HasNextActionRequest())
{
    StartRequest(Completion.GetNextActionRequest());
}
```

A complete, playable example lives in [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox): it drives CadenceArc with Enhanced Input and a Timer-based demo executor, including hold input.

## Documentation

The detailed docs are written in Chinese.

| Document | Covers |
| --- | --- |
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

Roadmap:

1. More expiry policies, and an optional combo timeout that returns to the entry.
2. Optional execution adapters, including GAS.
3. Input recording, replay, networking, and prediction research.

## Requirements

- Unreal Engine 5.7 and a supported C++ toolchain
- Git LFS for binary Unreal assets
