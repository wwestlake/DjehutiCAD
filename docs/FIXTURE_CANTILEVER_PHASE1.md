# Mechanical fixture cantilever milestone

The fixture's existing OCAF configuration, protected source resolution, SHA-256 source revisions,
material snapshots, command dispatch and stamped results remain authoritative. `linearStatic` now
dispatches to a bounded beam backend. No mesh or general FEA model is introduced.

## Supported experiment

1. Save a CAD **Box**, for example 400 x 20 x 10 mm, with an isotropic material (E = 200000 MPa).
2. Open **Test Fixtures**, create a fixture, add that saved CAD source and its Box as a part occurrence.
3. Select **Pick faces**. Pick the X-min end, choose `fixed`, and save the equipment. Use **New item**,
   pick X-max, choose `force`, set 100 N in source-document direction [0,0,-1], and save.
4. In **Rectangular cantilever**, set beam axis X, bending axis Y, display scale 5; click
   **Apply cantilever axes / display scale**, then **Run**.
5. Inspect tip displacement, clamp force/moment, maximum bending stress/strain and the moment table.
   The magenta curve is the calculated neutral axis multiplied by the displayed scale; the solid stays
   undeformed. Save the `.djfixture`; reopening retains the stamped result and the curve while current.

Face picking creates an explicit face-centre anchor. Editing its coordinates away from the centre
converts it to an exact-point anchor. For extra probes, choose **New item** before picking their face.
Displacement probes must be on the section centreline; reaction probes at the fixed-end centre.
Stress/strain probes report the signed longitudinal response at their actual section fibre: a
centreline stress probe correctly reads zero. The maximum stress/strain in the main result is the
extreme-fibre envelope. Safety factor and other unimplemented measurements remain unsupported.

**Pick bodies** selects occurrences. **Pick equipment** selects a marker for editing; coincident
markers can always be selected individually in the equipment list. **Move in world XY/XZ/YZ** enables
the white occurrence-origin handle. Drag previews translation and updates the numeric fields; release
commits one fixture OCAF edit. Use numeric placement for rotation. Orbit, pan, zoom and Fit all use the
existing viewport. Whole assembly occurrences can be translated; source assembly children stay intact.

## Structural formulation and limits

`CadCantilever` takes SI length, breadth, depth, Young's modulus and signed transverse force. Breadth
is parallel to the explicitly chosen bending axis, depth parallel to the force. Geometry comes from
the authored Box parameters and its evaluated placement, not a bounding-box guess or volume inertia.

For 0 <= x <= L: A=bh, I=bh^3/12, c=h/2, v=Fx^2(3L-x)/(6EI), rotation=Fx(2L-x)/(2EI),
M=F(L-x), longitudinal stress=-My/I, strain=stress/E. Reactions are -F and -L cross F in fixture-world
coordinates. Forty intervals sample the calculated shape and moment distribution.

Assumptions: one straight, homogeneous prismatic rectangular beam, ideal six-DOF clamp, force at the
opposite end-face centre, principal-axis bending, linear elasticity and small displacement. Either end
can be fixed. Both source placement and fixture placement are accounted for. Minimum L/largest-section
dimension is 10; tip displacement/L must be <= 0.05 and tip slope <= 0.1 rad. If yield strength is present,
stress reaching yield is rejected; otherwise the UI states that yield margin is unverified.

Unsupported: arbitrary/imported/modified sections, assembly or multiple-body structural solves,
partial supports, additional loads, axial/eccentric loads, torsion, gravity/self-weight, shear deflection,
geometric/material nonlinearity, contact and local clamp stress concentration. The fixture gravity vector
continues to apply to mass/self-weight property evaluation; it is explicitly excluded from this backend.
No failed or unsupported solve publishes a usable numerical table or deformation curve.

## Attachment and revision integrity

Every face-attached item must resolve to exactly one persistent face and lie on its **trimmed** geometry
within 1e-5 mm. Acceptance remaps only an explicit face-centre anchor on a surviving planar face whose
centre lies on that face. Exact points stay where authored if still valid; off-face, split, missing or
ambiguous attachments retain configuration with a correction issue. Invalid markers are red and block
analysis. Legacy points have no inferred remapping intent. No nearest-face retargeting occurs.

Source changes stale previous results before acceptance. Acceptance is a fixture OCAF transaction;
undo restores the prior configuration, whose older source digest remains invalid against the changed
file. A corrected equipment save clears its issue, followed by geometric validation. Source CAD files
are only read (loader staging remains in TEMP).

## Verification

`--cad-fixture-cantilever-check` exercises the modular solver and fixture integration, including
independent N/mm/MPa benchmarks, scaling, signed loads, reactions, invalid inputs, material substitution,
stamped persistence, revision remapping and rotated coordinate frames. `--cad-fixture-check` retains
the prior fixture regression checks. `--cad-fixture-ui-check` is explicitly a **programmatic component**
check; it is not evidence of native Windows interaction. Adding `--interactive` prepares a protected
400 x 20 x 10 beam with material and one occurrence, without invoking GUI control callbacks.

Validation on 2026-10-21:

| L (mm) | b x h (mm) | F (N) | E (MPa) | Expected / calculated tip (mm) | Expected / calculated maximum stress (MPa) |
|---|---|---|---|---|---|
| 400 | 20 x 10 | 100 | 200000 | 6.4 / 6.4 | 120 / 120 |
| 800 | 20 x 10 | 25 | 200000 | 12.8 / 12.8 | 60 / 60 |
| 400 | 30 x 20 | 200 | 70000 | 3.047619048 / 3.047619048 | 40 / 40 |

Debug build passed. The existing local cache uses `/DEBUG:NONE /INCREMENTAL:NO` to limit linker memory;
no project-wide compiler/linker setting was changed. Automated checks passed: 48 cantilever engineering
and integration checks, 41 existing fixture checks, and 15 JUCE component checks. These cover numerical
results, independent units, equilibrium, source protection, accepted revisions, stamped save/reopen,
property controls, drag preview/commit/single-step undo and numerical result text. The component render
was inspected for layout; it excludes native OpenGL pixels.

Native testing was attempted with a separate `--cad-fixture-ui-check --interactive` process. The helper
located its unique window, but capture failed with `FrameArrived timed out: timed out waiting on channel`.
A fresh window binding and one retry failed with `window capture timed out: timed out waiting on channel`.
No native mouse or keyboard action was issued. The helper was reset immediately and only the owned test
process was closed. It remains **unverified through native operation** that users can complete fixture
creation/open/save/tab selection/close, camera and body/face/marker picking, occurrence dragging, material
selection, load/support setup, analysis execution, deformation inspection and save/reopen. Automated
checks are separate evidence and do not satisfy that native acceptance step. The smallest remaining
step is the representative Windows GUI walkthrough above in a session with working desktop capture.
