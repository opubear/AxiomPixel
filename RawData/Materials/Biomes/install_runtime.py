"""Restore the eight runtime PNG copies from the preserved, hash-checked sources."""
import hashlib
import json
from pathlib import Path
import shutil


def main():
    root = Path(__file__).resolve().parents[3]
    target = root / "Pixel/Content/Art/Biomes"
    record = json.loads((target / "SOURCE.json").read_text())
    resources = [(root / item["Source"], target / item["Path"], item["SHA256"])
                 for item in record["Resources"]]
    # Validate the entire set before replacing any runtime resource.
    for source, _, expected in resources:
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"source checksum mismatch: {source}")
    for source, destination, _ in resources:
        shutil.copyfile(source, destination)
    print(f"Restored {len(resources)} biome textures into {target}")


if __name__ == "__main__":
    main()
