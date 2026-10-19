# Workspace menus, active tools and reference guides

Bill authorized Codex to take over after Claude completed and merged face sketching and manufacturing export (October 18, 2026).

## Deliverable

Replace permanent action-button rows with tab-owned menus that travel with detachable workspaces. Keep application menus for system/document/window/account/help commands. Provide a dockable active-tool Toolbox (selection first, then sketch tools), context-sensitive right-click actions, and clearly selected tools. Parts catalogs remain a separate future feature.

Add a default world XY floor grid, optional component-local XY/XZ/YZ reference planes and axes, and joint reference/motion guides from actual solved frames. Visibility is presentation state, separate from snapping and authored OCAF data. Preserve rendering on change, selection transforms, independent cameras, existing modeling commands and external-motion ownership.

Selection tools: pointer, box and circle; camera gestures remain available independently. Local menus and tool actions share dispatch with existing commands. Persist workspace/toolbox/view preferences without creating document undo entries.

## Rules and closeout

Use the shared checkout and outputs; preserve unrelated files and untracked work. Work on a feature branch, use single-worker incremental Debug builds with output redirected to logs and completion waits (no process/log polling), and perform focused checks for changed behavior only. Reuse existing JUCE/OCCT infrastructure; no unrelated refactor, dependency rebuild, slicer or stock-library implementation. Keep command/agent/Help parity where applicable. Distinguish source/build/diagnostic/real GUI evidence. Commit specific files, push the branch, merge into main and push main, update the handoff and stop. Bill does not need commit hashes.
