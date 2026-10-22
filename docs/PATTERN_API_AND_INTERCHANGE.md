# Mirror and pattern API / interchange conventions

The three commands use CadCommandService and the existing OCAF transaction,
rebuild, geometry cache and topology-reference mechanisms. Stable public IDs
identify sources and targets; face construction tokens and Chamfer's persistent
edge tokens identify geometric references. Reference changes that remove or split
the chosen topology fail instead of silently choosing different geometry.

An empty `target` in feature mode follows the source operation's base target,
including later target reassignment. `resolvedTarget` in command replies reports
the current target ID. An explicit target overrides that binding. The GUI
**Follow source target** button restores automatic binding.

Each create/update/delete is one OCAF transaction. Preview runs the same command
on an independent draft document; live revision, modified state and Undo history
are unchanged. Dependent source/target/reference deletion is refused. Repeated
features are history features, never disconnected imported geometry. All
parameters and source/reference dependencies participate in cache invalidation.

Native `.djo` stores the structured definition in feature child 27. Engineering
`.djc` JSON **version 12** adds a `replication` object to `mirror`, `linearPattern`
and `circularPattern` features. Its schema matches complete API `parameters`:

```json
{"sources":["hole-2"],"operation":"feature","target":"box-1","includeSource":true,
 "direction1":{"type":"X"},"direction2":{"type":"Y"},"count1":3,"count2":2,
 "spacing1":20,"spacing2":30,"reverse1":false,"reverse2":false}
```

Older documents still load. Older applications cannot evaluate these feature
types. JSON remains an engineering interchange format; the GUI Save command
writes native `.djo`, and service persistence checks cover both formats.

Copies have stable role names of the form
`<pattern-id>/instance:<index>/source:<source-index>/<source-face-token>`.
Source order is the `sources` array order. Material operations carry surviving
target-face names and instance tool-face roles through OCCT history. A reference
that is split or removed can require reselection. Grid indices may change when
count1 changes; reference the returned definition and current face list.

Limits are 1–32 source solids, 2–256 positions (Mirror has two), and at most 512
body copies. Feature mode accepts one Hole, Cut, Boolean Subtract or Union.
Subtractive/additive Boolean features repeat their source material delta;
Hole/Cut repeat the actual cutter. It does not replay arbitrary feature trees,
fillets/chamfers, nested pattern operations or assembly occurrences in feature
mode. These can be repeated as whole bodies. Instances that no longer modify a
material target fail with the failing index. Overlapping cuts may merge when
each still removes material; fully duplicate/no-effect instances are refused.

GUI automation: commands `mirror`, `linearPattern`, `circularPattern`; selected
result button `repEdit`; slots `repSources`, `repTarget`; `repClearSources`,
`repOperation` (Bodies / Modeling feature / Add solids to target / Cut solids from
target), `repInclude`, `repPreview`, `repClearPreview`, Apply/Cancel.
Reference keys are `plane`, `direction1`, `direction2`, `axis`; each has `<key>Type`,
`<key>Pick`, and custom `<key>OX/OY/OZ`, `<key>DX/DY/DZ` controls.
Distribution controls use `rep_count1`, `rep_count2`, `rep_spacing1`,
`rep_spacing2`, `rep_reverse1`, `rep_reverse2`, or `rep_count`,
`rep_angleDegrees`, `rep_reverse`. Shared selection callbacks provide face/edge
picks. `state.replicationPreview` reports a displayed preview.

`--cad-replication-check` writes `build/cad-replication-check.result.json`. It
exercises exact solids/material, parameter rebuilding, repeated cuts/holes/bosses,
through-all across differing thicknesses, references, Undo/Redo, native/JSON,
invalid input rollback, actual GUI controls, FRust execution and compiled CAD-node
execution. Programmatic controls are distinct from native pointer/visual testing;
verification results are recorded after running the diagnostic.

Verified October 22, 2026: Debug build; 74/74 replication checks; 117/117 existing
modeling/face-attachment checks; 22/22 live HTTP/GUI-control checks passed. The
replication diagnostic executes real FRust and compiled CAD-node programs.
GUI forms and shared selection callbacks were exercised programmatically;
native pointer/visual interaction was not verified. See the
[completion report](PROFESSIONAL_MODELING_ORDER3.md).
