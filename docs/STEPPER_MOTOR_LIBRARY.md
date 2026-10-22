# Stepper motor library

Three ordinary component records extend the existing mechanical browser. Search by
part number, manufacturer, or Light/Medium/Heavy. Insert, transform, mate and save
them using the normal component/assembly tools; each document retains its own
definition snapshot. Existing user catalogs receive missing bundled IDs without
overwriting customized records. There is no motor-specific CAD generator.

`Resources/stepper-motor-reference.json` is the engineering reference shared byte
for byte with DEWB's `prototype/assets/stepper-motor-reference.json`. Each field
records units, provenance and confidence; unavailable values remain null. The
standard modeling-command recipes are in `Resources/components/source/`.

| Component | Drawing-based geometry (mm) | Confidence and omissions |
|---|---|---|
| 28BYJ-48 12 V reference | Body Ø28 × 19; shaft Ø5, double flat across 3, projection 15.2; offset 8; mounting pitch 35, holes Ø4; pilot Ø9.2 × 1.5 | Maintex family drawing, not an identified purchased SKU. Ear thickness 1 and plastic exit are illustrative. Exact supplier, gearing, electrical coefficients and mass are unknown. |
| STEPPERONLINE 17HS19-2004S1 | Maximum case 42.3 × 42.3 × 48; mounting pitch 31, M3 minor-diameter reference bores with 4.5 minimum depth; pilot Ø22 × 2; shaft Ø5, projection 24, flat length 15/across 4.5 | Manufacturer drawing A0842 rev 0. Nominal mass 0.390 kg. Threads/cosmetic details are simplified. |
| STEPPERONLINE 23HS30-2804S | Maximum case 57.3 × 57.3 × 76.5; pitch 47.14, holes Ø5.2; pilot Ø38.1 × 1.6; shaft Ø6.35, projection 21, flat length 15/across 5.85 | Current manufacturer drawing. Mounting-hole axial depth is unspecified: envelope cutters extend through the simplified body and are not a physical depth specification. Mass unavailable. |

Origin is the front mounting plane at the shaft axis; +Z points along the output
shaft, the housing extends into −Z. Light motor housing/mount centerline is Y=−8.
Reference metadata includes mounting plane, shaft axis and hole centers. OCCT faces
and cylindrical surfaces remain available to ordinary picking and mating tools.
Shaft projection includes the pilot region and is measured from the mounting plane.

All records are explicitly **simplified drawing-derived geometry**, not certified
manufacturing models. STEPPERONLINE's STEP downloads require a supplier login;
the drawing-derived alternative was used. Housing boxes bound cosmetic details.
Connector/service-loop dimensions are unavailable; no invented clearance envelope
is presented as verified. Check the physical connector and tolerances before
manufacturing a close-fitting enclosure or bracket. These are heterogeneous
assemblies: no invented homogeneous material density or center of gravity is assigned.

The current NEMA 23 source specifies 2.0 N·m, 1.17 Ω, 4.36 mH and a 76.5 mm case;
older 1.9 N·m/76 mm descriptions are superseded. Rotor inertias are 8.2e-6 and
3e-5 kg·m² for the NEMA 17 and 23 respectively. Commercial Kt/Ke and friction
remain unknown. The unidentified 12 V 28BYJ does not inherit a 5 V motor's data or
the electrical values of an arbitrarily selected Maintex variant.

Sources: [Maintex family](https://maintexmotors.com/product/28byj-48-stepper-motor/),
[NEMA 17 drawing](https://files.omc-stepperonline.com/17HS19-2004S1_Full_Datasheet.pdf),
[NEMA 23 drawing](https://files.omc-stepperonline.com/23HS30-2804S_Full_Datasheet.pdf).
Drawing tolerances and per-field notes are retained in the shared manifest.

Focused verification covers library discovery, actual OCCT shaft/hole geometry,
transformed instances, shared definitions, mounting bracket clearance, native `.djo`
reopen, metadata/datums, independent documents, undo/redo and legacy catalog upgrade.
The existing component, geometry and Craft checks pass (49 + 17 + 45).

The running Debug app also completed insertion of all three motors, a fourth
transformed occurrence, ordinary bracket modeling, and save/new/reopen through
its existing local API. OpenGL initialized successfully. Native window capture
failed twice (`FrameArrived timed out`, then `get_window_state` timeout), so this
does not verify visual appearance, native mouse picking or hands-on assembly use.
The agent-owned process was closed and the computer-use helper reset immediately.
