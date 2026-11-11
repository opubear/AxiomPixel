"""Restore prepared biome panoramas; no image-generation service is needed."""
from pathlib import Path
import hashlib
import json
import shutil

source = Path(__file__).resolve().parent
destination = source.parents[2] / "Pixel/Content/Art/BiomeBackgrounds"
manifest = json.loads((source / "SOURCE.json").read_text())
destination.mkdir(parents=True, exist_ok=True)
for asset in manifest["files"]:
    prepared = source / asset["runtime"]
    if hashlib.sha256(prepared.read_bytes()).hexdigest() != asset["sha256"]:
        raise ValueError(f"damaged prepared biome background: {prepared}")
    shutil.copy2(prepared, destination / prepared.name)
shutil.copy2(source / "SOURCE.json", destination / "SOURCE.json")
