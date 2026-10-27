"""Update the bundled material references from the researched Office workbook.

Usage: python tools/populate_engineering_materials.py path/to/DjehutiCAD_Robotics_Materials_Research_Catalog_2026-10-20.xlsx
The workbook is read only and is never copied into the application package.
"""

import json
import sys
from pathlib import Path

from openpyxl import load_workbook


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "Resources" / "engineering-reference.json"
PREFIX = "mat-research-"


def value(row, key):
    cell = row.get(key)
    return None if cell is None or cell == "" else cell


def property_value(number, unit):
    return {"value": number, "unit": unit}


def convert(row):
    source_id = str(row["ID"])
    if not source_id.startswith("MAT-"):
        raise ValueError(f"Unexpected source material ID: {source_id}")
    name = str(row["Material / Grade"])
    process = str(row["Process / Form"])
    orientation = str(row["Orientation"])
    family = "Aluminum" if "aluminum" in name.lower() else (
        "Printed polymer" if "FDM" in process else "Engineering plastic"
    )
    label = name + (f" — {orientation}" if orientation not in ("Unspecified", "Isotropic typical") else "") + " (reference)"
    properties = {}
    for source, target, unit in (
        ("Density (kg/m³)", "density", "kg/m^3"),
        ("Young's E (MPa)", "youngsModulus", "MPa"),
        ("Tensile Strength (MPa)", "tensileStrength", "MPa"),
        ("Yield Stress (MPa)", "yieldStrength", "MPa"),
    ):
        measured = value(row, source)
        if measured is not None:
            if not isinstance(measured, (int, float)) or measured <= 0:
                raise ValueError(f"{source_id}: invalid {source}")
            properties[target] = property_value(measured, unit)
    notes = [
        f"Research workbook {source_id}, checked {row['Checked Date']}.",
        f"Process/form: {process}; orientation: {orientation}.",
        f"Basis: {row['Test / Basis']}.",
        "Incomplete reference data; not a qualified isotropic constitutive model.",
    ]
    for key, unit in (("Break Stress (MPa)", "MPa"), ("Flexural Modulus (MPa)", "MPa"), ("HDT (°C)", "°C")):
        measured = value(row, key)
        if measured is not None:
            notes.append(f"Workbook {key}: {measured} {unit} (not a supported material-model property).")
    for key in ("Data Status", "Rights / Terms", "Notes / Cautions"):
        if value(row, key) is not None:
            notes.append(f"{key}: {row[key]}.")
    return {
        "id": PREFIX + source_id.lower(),
        "name": label,
        "revision": 1,
        "model": "referenceOnly",
        "family": family,
        "specification": name,
        "condition": f"{process}; {orientation}",
        "source": str(row["Source URL"]),
        "note": " ".join(notes),
        "properties": properties,
    }


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Expected the original materials research .xlsx path")
    sheet = load_workbook(sys.argv[1], read_only=True, data_only=True)["Engineering Materials"]
    rows = sheet.values
    columns = next(rows)
    if len(columns) != 17 or columns[0] != "ID" or columns[1] != "Material / Grade":
        raise ValueError("Unexpected materials workbook layout")
    # The Office source currently uses replacement characters in two unit headers.
    columns = tuple(c.replace("�", "³") if c and c.startswith("Density") else c.replace("�", "°") if c and c.startswith("HDT") else c for c in columns)
    additions = [convert(dict(zip(columns, row))) for row in rows if row[0]]
    ids = [item["id"] for item in additions]
    if len(ids) != len(set(ids)) or len(additions) != 29:
        raise ValueError("Expected 29 distinct material references")
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    catalog["materials"] = [m for m in catalog["materials"] if not m["id"].startswith(PREFIX)] + additions
    CATALOG.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Bundled {len(additions)} researched materials; catalog now has {len(catalog['materials'])} materials and {len(catalog['stocks'])} stock definitions")


if __name__ == "__main__":
    main()
