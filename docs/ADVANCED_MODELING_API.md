# Sweep, Loft and Router command / interchange conventions

The three tools use CadCommandService, OCCT evaluation and the existing OCAF
transaction, Undo/Redo, cache-invalidation and topology-history mechanisms.
They do not maintain a parallel editable document. GUI, HTTP/LLM tools, FRust
hosts and compiled CAD nodes call the same command methods.

Actions `create`, `get`, `update`, `delete` return structured `ok`, stable `id`,
`type` and complete `parameters`. Delete returns `deleted:true`; dependent source
deletion is refused. Update merges top-level fields; nested objects/arrays are
replaced in full. Errors return `ok:false`, `code` and `error`; failed evaluation
rolls back the transaction and preserves the valid document.

`preview` accepts create parameters, or an existing id plus edits. It evaluates
the same command on an isolated draft and returns `preview:true`,
`committed:false`, `volume_mm3` and a render snapshot in the GUI. It does not
change live revision, modified state, visibility or Undo history. Changing inputs
or Cancel restores the committed model. A model edit during a selection task
requires reopening that task to refresh references.

Native `.djo` stores the structured `modeling` definition in feature child 28.
Engineering `.djc` JSON **version 13** adds the `modeling` object to feature types
`sweep`, `loft` and `mill`; it matches the complete API parameter object. Existing
documents still load. Older applications cannot evaluate these new feature
types. The GUI saves native files; the existing service supports both formats.

Source sketches, ordered Loft sections, target solids and named reference faces
participate in dependencies. Paths use stable sketch entity IDs or the existing
persistent edge tokens used by Chamfer/patterns. Removed or ambiguous references
fail rather than silently attaching elsewhere. New surfaces have construction
roles and surviving target names are carried through OCCT history. Complex
topology changes can require reselection.

GUI automation commands: `sweep`, `loft`, `mill`. Result inspector:
`modelingEdit`. Common controls: `advTarget`, `advOperation` (New body / Add to
target / Subtract from target), `modelingPreview`, `modelingClearPreview`,
Apply/Cancel; `state.modelingPreview` reports displayed preview state.

- Sweep: `advProfile`, `advPath`, `advClearPath`, `advPathReverse`,
  `advOrientation`, `advTransition`, `adv_alignProfile`, `adv_rollDegrees`,
  `adv_twistDegrees`, `advBinormalX/Y/Z`.
- Loft: `advSections`, `adv_ruled`, `advAlignment`; each zero-based section i has
  `loftUp{i}`, `loftDown{i}`, `loftRemove{i}`, `loftOffset{i}`, `loftReverse{i}`,
  `loftSeam{i}`. Reordering moves correspondence with the section.
- Router: `advRegion`, `advPath`, `advStrategy`, `advReference`,
  `advReferencePick`, `advOriginX/Y/Z`, `advNormalX/Y/Z`, `advReferenceReverse`,
  `adv_alignToReference`, `adv_toolDiameter`, `adv_depth`, `adv_throughAll`,
  `advCompensation`.

`--cad-modeling-check` records focused results in
`build/cad-modeling-check.result.json`. Programmatic GUI controls/selection
callbacks are distinct from actual native pointer/visual verification. Verification
counts and remaining limitations are recorded after completing the checks.
