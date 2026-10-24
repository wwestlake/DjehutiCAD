# Sketch line interaction

The default Line tool uses click-click input: click the start, move without holding a button to preview, then click each connected endpoint. Each committed segment becomes the next start. Existing snapping and direction guides (H/V, Shift, exact angle) use the existing drawing path.

Press Escape or Enter, or use **End line sequence (Enter)** in the line properties, to discard the current preview while keeping all completed segments and the Line tool. Click again to start a separate sequence. Select or Finish Sketch also ends drawing. Repeating an endpoint does not create a zero-length segment.

The Sketch line properties contain **Line input** with **Click-click (connected)** and **Click-and-drag**. This is a workspace preference saved with existing application settings; it does not modify the CAD document. Changing modes drops an unfinished preview. Drag mode retains press-drag-release behavior and commits one segment per drag.

Segments continue through existing document commands, constraints, history, and file persistence. Programmatic begin/update/commit gestures retain their existing explicit transaction semantics. Other drawing tools and node-editor connections are unchanged.

## Validation

Focused checks are part of `--cad-sketch-usability-check` and exercise actual SketchCanvas mouse callbacks. Coverage includes first-click release, unpressed preview, connected closure, repeated endpoint rejection, Escape/Enter/button completion, individual segment undo/redo, save/reopen, both preference values, and legacy drag release. Existing sketch usability, advanced editing, and line-property checks provide constraint/snapping and unrelated-tool regression coverage.

Validated on 2026-10-24 with the Debug target: build succeeded (`build/sketch-click-line-build-accepted.log`). Final executable results: Sketch usability 56/56, line-properties repair 65/65, advanced Sketch editing 51/51; all three exited normally with code 0. Total: 172 passing checks. The advanced fixture now explicitly selects the optional drag preference for its legacy drag-only H/V/Shift checks. Initial failures in those three checks were due to that old default assumption; the accepted rerun is fully passing.

Validation uses synthetic JUCE input through the production component handlers, not physical mouse interaction. A fresh native desktop visual/manual session and a full application restart preference check were not performed. Preference persistence was checked through the existing workspace settings serialization and restoration, which the application saves on shutdown. No user document or node-editor source was modified.
