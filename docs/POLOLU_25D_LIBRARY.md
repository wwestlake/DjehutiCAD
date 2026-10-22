# Pololu 25D manufacturer components

Four separate ordinary component records are available in Mechanical / Motors /
DC Gearmotors / Pololu 25D. Search by the Pololu part number. Existing component,
assembly, OCCT picking/mating, OCAF undo and `.djo` snapshot persistence are reused.
No pre-existing record for these exact parts was found; no existing ID was renamed.

| Part | Canonical identity | Configuration | Manufacturer STEP |
|---|---|---|---|
| 4865 | `motor-dc-pololu-4865` | MP 12 V, encoder, 101 g | 0J1647 / `25d-metal-gearmotor-34-47-encoder.step` |
| 4885 | `motor-dc-pololu-4885` | LP 12 V, encoder, 101 g | Same shared manufacturer mechanical configuration |
| 4845 | `motor-dc-pololu-4845` | HP 12 V, encoder, 101 g | Same shared manufacturer mechanical configuration |
| 3229 | `motor-dc-pololu-3229` | MP 12 V, no encoder, 88 g | 0J1113 / `25d-metal-gearmotor-34-47.step` |

Original STEP, drawing, full performance datasheet and product HTML are preserved
under `Resources/components/manufacturer/pololu`. Source hashes, input archive
hashes, electrical differences, reference frames and geometry measurements are in
`Resources/pololu-25d-reference.json`, byte-identical to DEWB's reference manifest.
Each runtime record contains an embedded manufacturer BRep snapshot; assemblies
do not depend on Google Drive, loose STEP paths or the current library record.

Both source ZIPs passed independent per-member CRC checks. The two selected STEP
files matched their archive entries and inventory SHA-256 hashes. OCCT transferred
valid solid geometry: two solids in the encoder file, one in the non-encoder file.
Solids were retained without fusing them or replacing them with primitives.

The normalized origin is the gearbox mounting-face center. +Z follows the output
shaft; +Y points toward the D-flat. The encoder source already uses that frame.
The non-encoder source has its origin at (30.875, 0, 18.375) mm in the source frame;
it was translated by the negative of that vector, with no scaling or rotation.

Verified drawing dimensions: shaft Ø4, across-flat 3.5, projection 12.5; pilot Ø7
with 2.5 projection; gearbox Ø25 and length 21; motor can Ø24.4 and length 30.8;
two M3 mounting holes at (0, ±8.5, 0), pitch 17; maximum screw penetration 6 mm.
The face screws at (±8.5, 0, 0) are **not** the mounting holes.

The supplied STEP has nominal Ø2.5 minor bores and simplified thread/inner pocket
geometry. The drawing specifies M3 and 6 mm depth; no thread pitch, fit or
manufacturing tolerance is inferred from the model. Both STEP shafts have a flat
at Y=1.5, from Z=4.5 to 12.5 (8 mm axial length): this is **CAD-derived nominal**,
not a dimensioned or toleranced manufacturer shaft-flat specification.

Preserved geometry differences: encoder rear extent Z=−66.45, with supplied lead
stubs extending to Y=18.25356; non-encoder rear/terminal extent Z=−56. The drawing
body lengths are 66.5 and 51.8 respectively, with terminal clearance additional on
the non-encoder motor. Neither STEP was stretched to match rounded drawing values.
Encoder leads in the drawing are approximately 200 mm; the STEP's short stubs do
not establish flexible-cable routing or service clearance. Mass is published
component mass; density, center of gravity and mass inertia remain unknown.

`matingDatums` exposes the mounting plane, shaft axis, D-shaft dimensions and hole
centers. Actual OCCT faces remain selectable through the existing geometry tools.

Primary sources retained in the package:
[drawing](https://www.pololu.com/file/0J1634/25d-metal-gearmotor-dimension-diagram.pdf),
[encoder STEP archive](https://www.pololu.com/file/0J1647/25d-gearmotor-3d-models.zip),
[non-encoder archive](https://www.pololu.com/file/0J1113/25d-gearmotor-3d-models.zip),
[full datasheet](https://www.pololu.com/file/0J1829/pololu-25d-metal-gearmotors-rev-2-0.pdf).

Validation uses the existing headless component check: exact part discovery,
solid counts, shaft/flat/pilot/mount interfaces, production STEP re-import volume,
assembly transformations, OCAF undo/redo and native save/reopen. No GUI instance
or computer-control helper is launched by these checks.

Verified Debug build: existing component checks 77/77 and Craft checks 45/45
passed. These include four independent Pololu definitions and the actual
manufacturer re-import/persistence checks above. Native GUI use was not exercised.
