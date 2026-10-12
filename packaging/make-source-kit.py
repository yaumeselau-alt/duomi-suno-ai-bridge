"""Create a source-only kit using an allowlist, never the user's build/cache."""
from pathlib import Path
import hashlib
import json
import re
import stat
import zipfile

root = Path(__file__).resolve().parent.parent
version = re.search(r"VERSION\s+(\d+\.\d+\.\d+)", (root / "CMakeLists.txt").read_text()).group(1)
dist = root / "dist"
dist.mkdir(exist_ok=True)
archive = dist / f"Duomi-Suno-AI-Bridge-{version}-Mac-Source.zip"
prefix = f"Duomi-Suno-AI-Bridge-{version}-Source"
files = [root / "CMakeLists.txt"]
for name in ("src", "assets", "tests", "packaging", "lib/JUCE"):
    files.extend(p for p in (root / name).rglob("*") if p.is_file()
                 and not any(part in {".git", "__pycache__", ".cache", ".github"} for part in p.relative_to(root).parts))
manifest = []
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for path in sorted(set(files)):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise RuntimeError(f"Unexpected source link: {relative}")
        if path.suffix.lower() in {".dpapi", ".wav", ".mp3", ".m4a", ".pdb", ".log"}:
            # Vendored framework example assets are not user material.
            if not relative.startswith("lib/JUCE/"):
                raise RuntimeError(f"Unexpected private/generated material: {relative}")
        data = path.read_bytes()
        info = zipfile.ZipInfo(f"{prefix}/{relative}")
        info.create_system = 3
        mode = 0o755 if path.suffix == ".sh" else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
        manifest.append({"path": relative, "bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()})
    z.writestr(f"{prefix}/SOURCE-MANIFEST.json",
               json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8"))
    z.writestr(f"{prefix}/START-HERE.txt",
               ("Mac source kit (not an installer).\n"
                "Open packaging/INSTALL.txt and packaging/README.md.\n"
                "On macOS with Xcode and CMake: bash packaging/macos/build-package.sh\n"
                "Contains no private build outputs, API credentials or user audio.\n"))
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise RuntimeError("Source archive CRC verification failed")
    for entry in manifest:
        if hashlib.sha256(z.read(f'{prefix}/{entry["path"]}')).hexdigest() != entry["sha256"]:
            raise RuntimeError(f'Hash mismatch: {entry["path"]}')
deliverables = (sorted(dist.glob(f"Duomi-Suno-AI-Bridge-{version}-*.exe"))
                + sorted(dist.glob(f"Duomi-Suno-AI-Bridge-{version}-*.pkg")) + [archive])
lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in deliverables]
(dist / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
print(json.dumps({"archive": str(archive), "files": len(manifest),
                  "bytes": archive.stat().st_size, "hashes": lines}, indent=2))

