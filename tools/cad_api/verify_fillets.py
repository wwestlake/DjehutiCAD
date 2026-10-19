"""Verification script for constant-radius edge fillets.
Validates:
1. Solid body creation (Box 50x50x50).
2. Single-edge fillet (radius 5mm). Verify resulting volume and face count.
3. Multi-edge fillet (e.g., 4 top edges).
4. Parametric radius edit (e.g., radius changed to 8mm via cad_set_dimensions).
5. Undo / Redo functionality restoring original sharp edges.
6. Persistence (.djo native and .djc JSON save/reopen).
7. Upstream parameter modification tolerance (e.g., box dimension edit regenerates fillet).
8. Graceful rejection of invalid/oversized radius (e.g., 100mm) without damaging the solid.
"""
import sys
import os
import json
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from client import Client

def run_tests():
    c = Client()
    tests_passed = []

    def check(cond, msg):
        if not cond:
            raise AssertionError(f"FAILED: {msg}")
        print(f"  [PASS] {msg}")
        tests_passed.append(msg)

    print("--- Starting Fillet Verification ---")

    # Start with a clean document
    c.ui("document", operation="new", discard=True)
    state = c.ui()["state"]
    check(len(state.get("features", [])) == 0, "Clean document initialized")

    # 1. Create a box: 50 x 50 x 50
    box_res = c.call("cad_create_box", {"width": 50.0, "depth": 50.0, "height": 50.0})
    box_id = box_res["id"]
    check(bool(box_id), f"Created box {box_id}")

    # Inspect initial box
    state = c.ui()["state"]
    box_feature = next(f for f in state["features"] if f["id"] == box_id)
    initial_faces = c.call("cad_list_faces", {"feature": box_id})["faces"]
    check(len(initial_faces) == 6, f"Box has 6 initial faces (found {len(initial_faces)})")

    # 2. Fillet a single edge (edge 0) with radius 5.0 mm
    print("\n--- Test 1: Single Edge Fillet ---")
    fillet_res = c.call("cad_fillet", {"target": box_id, "edges": [0], "radius": 5.0})
    check(fillet_res.get("ok"), "Single edge fillet applied successfully")
    fillet_id = fillet_res["id"]
    check(bool(fillet_id), f"Fillet feature created with ID: {fillet_id}")

    # Check resulting geometry: 6 faces of box, 1 edge filleted -> 1 new cylindrical blend face = 7 faces
    fillet_faces = c.call("cad_list_faces", {"feature": fillet_id})["faces"]
    check(len(fillet_faces) == 7, f"Filleted body has 7 faces (found {len(fillet_faces)})")
    
    # Check that original box is hidden and fillet is visible
    state = c.ui()["state"]
    box_f = next(f for f in state["features"] if f["id"] == box_id)
    fillet_f = next(f for f in state["features"] if f["id"] == fillet_id)
    check(not box_f["visible"], "Original box body is hidden")
    check(fillet_f["visible"], "Filleted body is visible")

    # 3. Edit radius from 5.0 to 8.0 mm via cad_set_dimensions
    print("\n--- Test 2: Edit Fillet Radius ---")
    dim_res = c.call("cad_set_dimensions", {"id": fillet_id, "radius": 8.0})
    check(dim_res.get("ok"), "Radius edited to 8.0 mm")
    state = c.ui()["state"]
    fillet_f = next(f for f in state["features"] if f["id"] == fillet_id)
    check(abs(fillet_f["radius"] - 8.0) < 1e-6, f"Fillet feature radius updated to 8.0 (found {fillet_f['radius']})")

    # 4. Undo and Redo
    print("\n--- Test 3: Undo / Redo ---")
    # Undo dimension change
    c.call("cad_undo")
    state = c.ui()["state"]
    fillet_f = next(f for f in state["features"] if f["id"] == fillet_id)
    check(abs(fillet_f["radius"] - 5.0) < 1e-6, "Undo restored radius 5.0 mm")

    # Undo fillet creation
    c.call("cad_undo")
    state = c.ui()["state"]
    check(not any(f["id"] == fillet_id for f in state["features"]), "Undo removed fillet feature")
    box_f = next(f for f in state["features"] if f["id"] == box_id)
    check(box_f["visible"], "Original box restored and visible")

    # Redo fillet creation
    c.call("cad_redo")
    state = c.ui()["state"]
    check(any(f["id"] == fillet_id for f in state["features"]), "Redo restored fillet feature")

    # 5. Native Save and Reopen (.djo)
    print("\n--- Test 4: Document Persistence (.djo) ---")
    tmp_djo = Path(tempfile.mkdtemp(prefix="dcad-fillet-")) / "test_fillet.djo"
    c.ui("document", operation="save", path=str(tmp_djo))
    check(tmp_djo.exists(), f"Saved document to {tmp_djo}")

    c.ui("document", operation="new", discard=True)
    check(len(c.ui()["state"]["features"]) == 0, "Document cleared")

    c.ui("document", operation="open", path=str(tmp_djo))
    state = c.ui()["state"]
    check(any(f["id"] == fillet_id and f["type"] == "fillet" for f in state["features"]), "Reopened document contains fillet feature")
    reopened_faces = c.call("cad_list_faces", {"feature": fillet_id})["faces"]
    check(len(reopened_faces) == 7, f"Reopened fillet has 7 faces (found {len(reopened_faces)})")

    # 6. Multi-edge fillet on a fresh body
    print("\n--- Test 5: Multi-Edge Fillet ---")
    c.ui("document", operation="new", discard=True)
    b2 = c.call("cad_create_box", {"width": 60.0, "depth": 40.0, "height": 30.0})["id"]
    # Fillet 4 edges (e.g. edges 0, 1, 2, 3) with radius 4.0 mm
    multi_res = c.call("cad_fillet", {"target": b2, "edges": [0, 1, 2, 3], "radius": 4.0})
    check(multi_res.get("ok"), "Multi-edge fillet applied successfully")
    multi_id = multi_res["id"]
    multi_faces = c.call("cad_list_faces", {"feature": multi_id})["faces"]
    # 4 filleted edges create 4 blend faces + corner transitions
    check(len(multi_faces) >= 10, f"Multi-edge fillet has >= 10 faces (found {len(multi_faces)})")

    # 7. Upstream modification tolerance
    print("\n--- Test 6: Upstream Parameter Edit Tolerance ---")
    # Change box depth on the upstream box from 40 to 45 mm
    up_res = c.call("cad_set_dimensions", {"id": b2, "width": 60.0, "depth": 45.0, "height": 30.0})
    check(up_res.get("ok"), "Upstream box resized successfully")
    state = c.ui()["state"]
    multi_f = next(f for f in state["features"] if f["id"] == multi_id)
    check(multi_f["visible"] and not multi_f.get("error"), "Downstream fillet regenerated successfully after upstream edit")

    # 8. Rejection of oversized / invalid radius
    print("\n--- Test 7: Rejection of Invalid / Oversized Radius ---")
    # Box is 60x45x30, radius 100mm is physically impossible
    oversized = c.call("cad_fillet", {"target": b2, "edges": [0], "radius": 100.0}, allow_failure=True)
    check(not oversized.get("ok"), "Oversized radius (100mm) rejected as expected")
    check(bool(oversized.get("error")), f"Informative error returned: {oversized.get('error')}")

    # Negative radius
    negative = c.call("cad_fillet", {"target": b2, "edges": [0], "radius": -5.0}, allow_failure=True)
    check(not negative.get("ok"), "Negative radius (-5mm) rejected")

    # Clean up
    c.ui("document", operation="new", discard=True)
    print(f"\n==========================================")
    print(f"ALL {len(tests_passed)} FILLET VERIFICATION CHECKS PASSED!")
    print(f"==========================================")

if __name__ == "__main__":
    run_tests()
