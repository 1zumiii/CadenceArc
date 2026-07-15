# CadenceArc

CadenceArc is a tag-driven, execution-agnostic branching action framework for Unreal Engine.

It is designed to interpret semantic input tags, traverse a configurable action graph, and emit action tags without depending on the system that eventually executes those actions.

```text
InputTag -> configurable action graph -> ActionTag
```

## Design Goal

CadenceArc separates two responsibilities that are commonly coupled in action-game prototypes:

- deciding which action should occur next;
- executing the selected action.

The framework owns action-path resolution. Animation, Gameplay Ability System integration, collision handling, and damage processing remain outside the core runtime module.

The name reflects that design: player **cadence** shapes an **arc** through a branching action graph.

## Current Milestone

Phase 1 establishes the smallest complete resolution pipeline:

- accept a semantic input tag;
- locate the current node in a configured graph;
- resolve a matching transition;
- emit the target action tag;
- preserve state when resolution fails;
- reset to the configured entry node.

The following features are intentionally deferred:

- input buffering and timing windows;
- press, release, hold, pause, and directional conditions;
- action execution confirmation and rejection;
- cancellation and interruption handling;
- Gameplay Ability System adapters;
- networking and prediction;
- custom graph editor tooling.

## Architectural Boundary

The core runtime module may depend on Unreal Engine fundamentals and Gameplay Tags, but it must not depend on:

- `GameplayAbilities`;
- animation montages;
- a specific character or weapon class;
- collision or damage systems;
- the WarriorRPG project.

External systems consume the emitted `ActionTag` and report lifecycle information through adapters added in later milestones.

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
        `-- Private/
```

CadenceArc is developed and validated through the separate [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox) Unreal Engine project, where this repository is mounted as a Git submodule under `Plugins/CadenceArc`.

## Requirements

- Unreal Engine 5.7
- A supported Unreal Engine C++ toolchain
- Git LFS for binary Unreal assets

## Status

CadenceArc is currently in its initial framework-development stage. APIs, asset formats, and module boundaries may change before the first stable release.

