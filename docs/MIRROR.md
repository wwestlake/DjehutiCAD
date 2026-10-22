# Parametric Mirror

Choose a source body and **Modify > Mirror**. Select XY, XZ or YZ, a numeric
custom plane, or a named planar face. A custom plane uses a world origin in mm
and a nonzero normal vector. Choose `face`, click **Pick face / edge**, then pick
the planar face in a model view. These are world datum definitions; there is no
separate named datum-plane subsystem.

**Retain original** includes the source at position zero. Preview validates an
isolated draft. Apply creates one OCAF feature; Cancel restores the actual model.
Select the result and **Edit sources and parameters** to change sources, mode or
plane. Clear sources before selecting replacements. Source/reference bodies are
temporarily displayed for picking. Delete uses the normal history inspector.

The operation selector determines what is reflected:

- **Bodies:** copies selected solids, including multi-solid bodies, with proper
  reflected orientation. Sources remain in history, hidden. Original and mirrored
  copies are contained in the result when Retain original is enabled.
- **Modeling feature:** repeat one Hole, sketch Cut, Boolean Subtract or Union on
  its base target. Hole/Cut reuse their original cutter; through-all extent is
  recalculated at each instance. Boolean operations use their added/removed
  material. Editing the source operation rebuilds the mirrored operation.
- **Add solids to target / Cut solids from target:** use source solids as tools
  and explicitly select the target.

Unsupported feature types fail with an explanation; they can still be mirrored
as whole bodies. Empty, inverted, failed Boolean and off-target/no-effect
instances fail atomically. Bodies mode retains independent solids and can contain
overlapping copies; use add mode when a combined material result is wanted.

## API, FRust and CAD node

`cad_mirror` actions: `create`, `get`, `update`, `delete`, `preview`, `references`.
Create requires `parameters.sources` (stable IDs). Other defaults: operation
`bodies`, includeSource `true`, plane `{type:"YZ"}`. Update merges top-level
parameters; reference objects are replaced in full. Feature mode with omitted/empty target follows the source's base, including
later target reassignment; replies report `resolvedTarget`. Explicit targets
override that binding; other material modes require a target.

Example: reflect an existing bracket across XZ and retain both halves:

```json
{"action":"create","parameters":{"sources":["bracket-id"],"plane":{"type":"XZ"},"includeSource":true}}
{"action":"update","id":"mirror-id","parameters":{"plane":{"type":"custom","origin":[50,0,0],"vector":[1,0,0]}}}
{"action":"get","id":"mirror-id"}
{"action":"delete","id":"mirror-id"}
```

Face plane: `{type:"face",feature:"box-1",face:"box-1/right"}`; discover tokens
with `cad_list_faces`. Preview uses the same arguments as create, or id plus
edits for an existing feature, and returns `preview:true`, `committed:false` and
volume without adding a live feature. Success returns `ok`, stable `id`, `type`,
complete normalized `parameters`, `instanceCount` and `instanceIndices`.
Errors return `ok:false`, `code` and `error` with the valid document preserved.

FRust: `djehuti_cad_mirror(args_json: String) -> String`. The **Mirror** CAD node
takes and returns the same JSON. All paths call CadCommandService.

See [pattern/interchange conventions](PATTERN_API_AND_INTERCHANGE.md) for
dependencies, persistence, instance names, validation limits and GUI field IDs.
