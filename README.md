# BlueprintJsonExport

`BlueprintJsonExport` is an Unreal Editor plugin that exports Blueprint graphs into compact JSON for AI tooling.
It is meant for machine consumption, code review workflows, and offline analysis. It is not a Blueprint runtime system.

Tested on Unreal Engine 5.4.
Linux is included in the platform allow list, but only 5.4 has been verified.
Compatibility with newer Unreal versions is not guaranteed yet.

## Human Summary

- Export selected Blueprints to deterministic JSON.
- Keep the payload compact enough for LLMs.
- Preserve the raw graph structure.
- Add a semantic index that makes control flow and dependencies easier for agents to understand.
- Run as a commandlet from the editor binary.

## Repository Layout

This repository is the plugin folder itself.
Install it into an Unreal project as:

```text
Plugins/BlueprintJsonExport
```

## Agent Summary

Read this section first if you are an agent.

- The output is AI-oriented JSON, not a human-facing interchange format.
- `g` is the canonical graph payload.
- `i` is an additive semantic index for faster reasoning.
- Prefer `i` for high-level understanding, then drill into `g` for exact pin-level logic.
- Do not assume every graph has `t`, `x`, or `i`.
- String-like fields in `g`, `m`, `t`, and `x` are stored through the global string table `s`.

## Features

- Commandlet export: `-run=BlueprintJsonExport`
- Deterministic graph ordering
- Compact tuple-based graph schema
- Optional semantic index for control-flow and dependency summaries
- Depth-limited recursive graph traversal
- Blueprint-defined struct and enum export
- Internal-only C++ surface; there is no supported public module API

## Install

Copy this folder into your Unreal project under:

```text
Plugins/BlueprintJsonExport
```

Then enable `Blueprint JSON Export` in the editor.

## Quick Start

```bash
"/Path/To/UnrealEditor" \
  "/Path/To/Project.uproject" \
  -run=BlueprintJsonExport \
  -Blueprints=/Game/UI/Menu/Components/BP_AvatarListEntry.BP_AvatarListEntry
```

Default output directory:

- `<Project>/Saved/BlueprintExports`

## Commandlet Arguments

```bash
"/Path/To/UnrealEditor" \
  "/Path/To/Project.uproject" \
  -run=BlueprintJsonExport \
  [-Blueprints=<objPath+objPath+...>] \
  [-Folder=<packagePath+packagePath+...>] \
  [-OutputDir=<path>] \
  [-Depth=<int|-1>] \
  [-Pretty=<true|false>]
```

Arguments:

- `-Blueprints`: explicit Blueprint object paths, joined by `+`
- `-Folder`: recursive package paths, joined by `+`
- `-OutputDir`: export directory, default `<Project>/Saved/BlueprintExports`
- `-Depth`: recursion depth limit, default `5`, use `-1` for unlimited
- `-Pretty`: pretty-print JSON, default `false`

Selection behavior:

- Export target set is the deduplicated union of `-Blueprints` and `-Folder`
- If both are omitted, the commandlet returns an error

## Path Formats

### `-Blueprints`

Accepted examples:

- `/Game/MyFolder/BP_Thing.BP_Thing`
- `/Game/MyFolder/BP_Thing`
- `Blueprint'/Game/MyFolder/BP_Thing.BP_Thing'`

### `-Folder`

Accepted examples:

- `/Game/UI`
- `Game/UI`

## Output Schema

Top-level keys:

- `v`: schema version, currently `1`
- `s`: global string table, with `s[0] == ""`
- `m`: metadata tuple
- `g`: graph array
- `t`: optional types tuple `[structs, enums]`
- `x`: optional skipped graph entries caused by truncated traversal
- `i`: optional semantic index for reasoning-oriented consumers

Tuple definitions:

1. `m`: `[bp_name, package_path, object_path, generated_class, bp_type, max_depth, truncated_flag]`
2. graph: `[graph_name, graph_type, nodes, links]`
3. node: `[node_type, title, member_parent, member_name, node_flags, pins]`
4. pin: `[dir, pin_type, container_type, name, sub_type, default_value, pin_flags]`
5. link: `[kind, src_node_idx, src_pin_idx, dst_node_idx, dst_pin_idx]`
6. `t`: `[structs, enums]`
7. struct: `[name, comment, members]`
8. struct member: `[name, member_type, type_name, member_flags, key_type, key_type_name, default_value, comment]`
9. enum: `[name, comment, values]`
10. enum value: `[name, comment]`
11. `x` entry: `[owner_graph_name, skipped_graph_or_function_name, skipped_owner_path, reason]`

All string-like tuple values are indexes into `s`.

`truncated_flag` is `1` when traversal omitted one or more child graphs for any reason recorded in `x`.

### Pin Schema

The current pin schema separates container shape from the base pin type.

- `pin_type`: scalar/object/struct-style pin kind
  Container values are no longer encoded here.
- `container_type`:
  - `0`: none
  - `1`: array
  - `2`: set
  - `3`: map
- `pin_flags`:
  - bit `0`: connected
  - bit `1`: reference
  - bit `2`: const

`x` reasons currently include:

- `depth_limit`
- `external_blueprint_graph`

For `depth_limit`, `skipped_owner_path` is empty because the skipped graph may not belong to a different asset.

### Semantic Index

`i` is redundant by design. It trades payload size for faster interpretation.

`i` contains:

- `schema`: semantic index schema version, currently `1`
- `graphs`: graph summaries in export order

Each semantic graph contains:

- `graph`: graph index into `g`
- `name`: plain-text graph name
- `kind`: readable graph kind such as `event_graph` or `function`
- `entryNodes`: node indexes that begin control flow
- `summary`:
  - `calls`
  - `nativeCalls`
  - `latentTasks`
  - `refs.classes`
  - `refs.assets`
  - `refs.tags`
- `nodes`:
  - `id`
  - `kind`
    Common values include `event`, `entry`, `exit`, `flow`, `delegate`, `latent_task`, `variable_get`, `variable_set`, `call_blueprint`, `call_native`, and `node`.
  - `title`
  - `memberParent`
  - `memberName`
- `exec`:
  - `from`
  - `fromPin`
  - `to`
  - `toPin`

## Agent Reading Strategy

1. Read `i.graphs[*].summary` to identify the graph purpose.
2. Read `i.graphs[*].entryNodes` and `i.graphs[*].exec` to understand control flow.
3. Use `i.graphs[*].nodes` to map semantic node roles.
4. Fall back to `g` when you need exact pin-level data or data-flow details.
5. Use `s` to decode tuple indexes.

## Determinism

- Graph order is stable.
- Node and pin order are preserved as encountered.
- String table insertion order is first-seen order.
- `t`, `x`, and `i` are omitted when empty.
- Output files mirror package paths under the chosen output directory, avoiding flat-name collisions.
- Relative `-OutputDir` values are resolved against the project root.

## Limitations

- Tested on Unreal 5.4 only.
- The semantic index helps with control flow more than data flow.
- The semantic index is a derived convenience layer, not the single source of truth.

## Repository Hygiene

This repository is intended to contain source, docs, and small static resources only.
Do not commit generated Unreal build outputs such as `Binaries/` or `Intermediate/`.

## License

MIT. See [LICENSE](./LICENSE).
