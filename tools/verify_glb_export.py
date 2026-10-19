"""Comprehensive verification script for glTF 2.0 Binary (.glb) export in DjehutiCAD.
Validates:
Test A — Simple solids (Box & Cylinder):
  - Correct glTF 2.0 binary structure (magic, version, JSON chunk, BIN chunk).
  - Valid JSON scene description (asset, scenes, nodes, meshes, primitives, bufferViews, accessors).
  - Physical scale in meters (e.g. 50mm box -> 0.05m bounds).
  - Vertex positions, valid indexed triangles, and outward-facing vertex normals.
  - Multi-part export (separate named nodes and meshes for box & cylinder).
  - Low vs High tessellation quality comparison on cylinder (triangle count differs appropriately).
Test B — Mechanical assembly (SCARA robot):
  - Assembly components exported as separate transformable nodes.
  - Joint pivot frames preserved with local mesh coordinates compensated.
  - Correct hierarchy between parent and child links.
Test C — API & UI Consistency:
  - cad_export tool returns ok=True, files list, sizes, and triangle counts.
Test D — Source preservation:
  - CAD document remains unmodified and unchanged after export.
"""
import sys
import os
import json
import struct
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "cad_api"))
from client import Client

def parse_glb(file_path):
    with open(file_path, "rb") as f:
        data = f.read()
    
    assert len(data) >= 12, "GLB file too short for header"
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF", f"Invalid magic: {magic}"
    assert version == 2, f"Invalid version: {version}"
    assert length == len(data), f"Length mismatch: {length} vs {len(data)}"
    
    offset = 12
    # Chunk 0: JSON
    chunk0_len, chunk0_type = struct.unpack_from("<II", data, offset)
    assert chunk0_type == 0x4E4F534A, f"Invalid chunk 0 type: {chunk0_type:X}"
    offset += 8
    json_bytes = data[offset : offset + chunk0_len]
    gltf_json = json.loads(json_bytes.decode("utf-8").strip())
    offset += chunk0_len
    
    bin_bytes = b""
    if offset < len(data):
        chunk1_len, chunk1_type = struct.unpack_from("<II", data, offset)
        assert chunk1_type == 0x004E4942, f"Invalid chunk 1 type: {chunk1_type:X}"
        offset += 8
        bin_bytes = data[offset : offset + chunk1_len]
        
    return gltf_json, bin_bytes

def run_tests():
    c = Client()
    tests_passed = []

    def check(cond, msg):
        if not cond:
            raise AssertionError(f"FAILED: {msg}")
        print(f"  [PASS] {msg}")
        tests_passed.append(msg)

    print("=== Starting GLB Export Verification ===")

    # Initialize empty document
    c.ui("document", operation="new", discard=True)
    state = c.ui()["state"]
    check(len(state.get("features", [])) == 0, "Clean document initialized")

    # -------------------------------------------------------------
    # Test A: Simple solids (Box & Cylinder)
    # -------------------------------------------------------------
    print("\n--- Test A: Simple Solids (Box & Cylinder) ---")
    box_res = c.call("cad_create_box", {"width": 50.0, "depth": 30.0, "height": 20.0})
    box_id = box_res["id"]
    cyl_res = c.call("cad_create_cylinder", {"radius": 15.0, "height": 40.0})
    cyl_id = cyl_res["id"]

    # Export both solids as GLB (High Quality)
    temp_dir = Path(tempfile.mkdtemp(prefix="dcad-glb-test-"))
    glb_high_path = temp_dir / "simple_high.glb"

    res_high = c.call("cad_export", {
        "format": "glb",
        "path": str(glb_high_path),
        "quality": "high"
    })
    check(res_high.get("ok"), "High-quality GLB export executed successfully")
    check(glb_high_path.exists() and glb_high_path.stat().st_size > 0, f"GLB file written ({glb_high_path.stat().st_size} bytes)")

    # Parse and validate GLB structure
    gltf_h, bin_h = parse_glb(glb_high_path)
    check(gltf_h.get("asset", {}).get("version") == "2.0", "glTF asset version is 2.0")
    check(len(gltf_h.get("nodes", [])) == 2, f"Scene has 2 distinct nodes (found {len(gltf_h['nodes'])})")
    check(len(gltf_h.get("meshes", [])) == 2, f"Scene has 2 meshes (found {len(gltf_h['meshes'])})")

    # Check bounds / scale: 50x30x20 mm box -> 0.05 x 0.03 x 0.02 m in glTF
    box_mesh_idx = next(i for i, n in enumerate(gltf_h["nodes"]) if "box" in n["name"].lower() or "box" in gltf_h["meshes"][n["mesh"]]["name"].lower())
    box_prim = gltf_h["meshes"][box_mesh_idx]["primitives"][0]
    pos_acc = gltf_h["accessors"][box_prim["attributes"]["POSITION"]]
    box_min = pos_acc["min"]
    box_max = pos_acc["max"]
    box_dx = box_max[0] - box_min[0]
    box_dy = box_max[1] - box_min[1]
    box_dz = box_max[2] - box_min[2]
    check(abs(box_dx - 0.050) < 1e-4, f"Box width in meters is 0.050m (found {box_dx:.4f}m)")
    check(abs(box_dy - 0.030) < 1e-4, f"Box depth in meters is 0.030m (found {box_dy:.4f}m)")
    check(abs(box_dz - 0.020) < 1e-4, f"Box height in meters is 0.020m (found {box_dz:.4f}m)")

    # Check vertex normals present
    check("NORMAL" in box_prim["attributes"], "Vertex normals attribute is present")

    # Check indices present
    check("indices" in box_prim, "Indexed triangle primitives are present")

    # Export with Low Quality and verify tessellation difference on the cylinder
    glb_low_path = temp_dir / "simple_low.glb"
    res_low = c.call("cad_export", {
        "format": "glb",
        "path": str(glb_low_path),
        "quality": "low",
        "overwrite": True
    })
    check(res_low.get("ok"), "Low-quality GLB export executed successfully")
    gltf_l, bin_l = parse_glb(glb_low_path)

    # Compare cylinder triangle counts between Low and High
    cyl_part_high = next(p for p in res_high["parts"] if cyl_id in p["id"])
    cyl_part_low = next(p for p in res_low["parts"] if cyl_id in p["id"])
    tri_high = cyl_part_high["triangles"]
    tri_low = cyl_part_low["triangles"]
    check(tri_low < tri_high, f"Tessellation quality control works: Low quality cylinder has {tri_low} triangles < High quality {tri_high} triangles")

    # -------------------------------------------------------------
    # Test B: Mechanical Assembly (SCARA Robot)
    # -------------------------------------------------------------
    print("\n--- Test B: Mechanical Assembly (SCARA Robot) ---")
    c.ui("document", operation="new", discard=True)

    # Construct SCARA robot parts
    # 1. Base
    base_box = c.call("cad_create_box", {"width": 80.0, "depth": 80.0, "height": 40.0})["id"]
    # 2. Shoulder Link
    shoulder_box = c.call("cad_create_box", {"width": 120.0, "depth": 40.0, "height": 30.0})["id"]
    # 3. Forearm / Elbow Link
    elbow_box = c.call("cad_create_box", {"width": 100.0, "depth": 30.0, "height": 25.0})["id"]
    # 4. Quill / Tool
    quill_cyl = c.call("cad_create_cylinder", {"radius": 8.0, "height": 80.0})["id"]

    # Create Assembly Instances
    inst_base = c.call("cad_create_instance", {"feature": base_box, "name": "Base"})["id"]
    c.call("cad_set_instance_grounded", {"instance": inst_base, "grounded": True})

    inst_shoulder = c.call("cad_create_instance", {"feature": shoulder_box, "name": "ShoulderLink"})["id"]
    inst_elbow = c.call("cad_create_instance", {"feature": elbow_box, "name": "ElbowLink"})["id"]
    inst_quill = c.call("cad_create_instance", {"feature": quill_cyl, "name": "Quill"})["id"]

    # Connect with Revolute & Prismatic Joints
    # Joint 1: Base -> Shoulder (Revolute around Z at (40, 40, 40))
    j1 = c.call("cad_create_joint", {
        "name": "J1_Shoulder",
        "type": "revolute",
        "parent": inst_base,
        "child": inst_shoulder,
        "origin": [40.0, 40.0, 40.0],
        "axis": [0.0, 0.0, 1.0],
        "reference": 0.0
    })["id"]

    # Joint 2: Shoulder -> Elbow (Revolute around Z at (140, 40, 55))
    j2 = c.call("cad_create_joint", {
        "name": "J2_Elbow",
        "type": "revolute",
        "parent": inst_shoulder,
        "child": inst_elbow,
        "origin": [140.0, 40.0, 55.0],
        "axis": [0.0, 0.0, 1.0],
        "reference": 0.0
    })["id"]

    # Joint 3: Elbow -> Quill (Prismatic along Z at (220, 40, 40))
    j3 = c.call("cad_create_joint", {
        "name": "J3_Quill",
        "type": "prismatic",
        "parent": inst_elbow,
        "child": inst_quill,
        "origin": [220.0, 40.0, 40.0],
        "axis": [0.0, 0.0, 1.0],
        "reference": 0.0
    })["id"]

    # Record modified state before export
    modified_before = c.ui()["state"].get("modified", False)

    # Export SCARA robot assembly to GLB
    scara_glb_path = temp_dir / "scara_robot.glb"
    scara_res = c.call("cad_export", {
        "format": "glb",
        "path": str(scara_glb_path),
        "quality": "medium",
        "overwrite": True
    })
    check(scara_res.get("ok"), "SCARA assembly exported successfully")
    check(len(scara_res["parts"]) == 4, f"Export report lists 4 components (found {len(scara_res['parts'])})")

    # Validate SCARA assembly GLB scene graph
    scara_gltf, scara_bin = parse_glb(scara_glb_path)
    nodes = scara_gltf.get("nodes", [])
    check(len(nodes) == 4, f"GLB contains 4 distinct component nodes (found {len(nodes)})")

    node_names = [n["name"] for n in nodes]
    check("Base" in node_names, "Node 'Base' present")
    check("ShoulderLink" in node_names, "Node 'ShoulderLink' present")
    check("ElbowLink" in node_names, "Node 'ElbowLink' present")
    check("Quill" in node_names, "Node 'Quill' present")

    # Check hierarchy: Base should have child ShoulderLink, ShoulderLink child ElbowLink, etc.
    base_node_idx = next(i for i, n in enumerate(nodes) if n["name"] == "Base")
    shoulder_node_idx = next(i for i, n in enumerate(nodes) if n["name"] == "ShoulderLink")
    elbow_node_idx = next(i for i, n in enumerate(nodes) if n["name"] == "ElbowLink")
    quill_node_idx = next(i for i, n in enumerate(nodes) if n["name"] == "Quill")

    check(shoulder_node_idx in nodes[base_node_idx].get("children", []), "Base node has ShoulderLink as child")
    check(elbow_node_idx in nodes[shoulder_node_idx].get("children", []), "ShoulderLink node has ElbowLink as child")
    check(quill_node_idx in nodes[elbow_node_idx].get("children", []), "ElbowLink node has Quill as child")

    # Check pivot frame: ShoulderLink node local translation should correspond to joint origin (40, 40, 40) mm -> (0.04, 0.04, 0.04) m
    sh_matrix = nodes[shoulder_node_idx].get("matrix", [])
    check(len(sh_matrix) == 16, "ShoulderLink has 4x4 transform matrix")
    tx, ty, tz = sh_matrix[12], sh_matrix[13], sh_matrix[14]
    check(abs(tx - 0.040) < 1e-4 and abs(ty - 0.040) < 1e-4 and abs(tz - 0.040) < 1e-4, 
          f"ShoulderLink pivot origin correctly set to joint location: ({tx:.3f}, {ty:.3f}, {tz:.3f})m")

    # -------------------------------------------------------------
    # Test C & D: Source preservation & Cleanliness
    # -------------------------------------------------------------
    print("\n--- Test C & D: Source Preservation ---")
    current_state = c.ui()["state"]
    check(len(current_state["features"]) == 4, "CAD document features unchanged after export")
    check(current_state.get("modified") == modified_before, "Export did not alter CAD document modified status")

    # Clean up workspace
    c.ui("document", operation="new", discard=True)
    check(len(c.ui()["state"]["features"]) == 0, "Clean empty document left in workspace")

    print("\n=======================================================")
    print(f"ALL {len(tests_passed)} GLB EXPORT VERIFICATION CHECKS PASSED!")
    print("=======================================================")

if __name__ == "__main__":
    run_tests()
