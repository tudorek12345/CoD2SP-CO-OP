#!/usr/bin/env python3
"""Stage independently authored native SP UI into an isolated fs_homepath.

Original menu bytes are read from a legitimate installation and patched only in
runtime output. Generated main.menu/menus.txt contain original content and must
never be added to the distributable. Requires Python 3.9+; no third-party modules.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import zipfile

EXPECTED = {
    "ui/main.menu": "5944b297bf4d1aa06836269c3f06aa14402646cfe4d73660354692a5b00b748f",
    "ui/menus.txt": "2db6349c8802b2cd5d9556086b47b66f16b0ba38cce863ae3be2a5b92b51a7e9",
}
MENUS = {"coop_main"}
MARKER = "COD2 CO-OP native UI generated locally"
MANIFEST = "coop-ui-manifest.json"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def discover_game() -> Path:
    candidates = [Path(r"C:\Program Files (x86)\Steam\steamapps\common\Call of Duty 2")]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
        candidates.append(steam / "steamapps/common/Call of Duty 2")
        library_file = steam / "steamapps/libraryfolders.vdf"
        if library_file.is_file():
            for value in re.findall(r'"path"\s+"([^"\r\n]+)"', library_file.read_text(encoding="utf-8")):
                library = Path(value.replace("\\\\", "\\"))
                candidates.append(library / "steamapps/common/Call of Duty 2")
    except (ImportError, OSError):
        pass
    for candidate in candidates:
        if (candidate / "CoD2SP_s.exe").is_file():
            return candidate.resolve()
    raise ValueError("Steam Call of Duty 2 installation not found; provide --game PATH")


def tokens(text: str):
    """Lex structural tokens while ignoring comments and quoted braces."""
    pattern = re.compile(r'//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z0-9_]*|[{}]', re.S)
    for match in pattern.finditer(text):
        token = match.group()
        if token.startswith("//") or token.startswith("/*"):
            continue
        yield token, match.start(), match.end()


def close_brace(ts, opening: int) -> int:
    if ts[opening][0] != "{":
        raise ValueError("Expected opening menu brace")
    depth = 0
    for i in range(opening, len(ts)):
        if ts[i][0] == "{":
            depth += 1
        elif ts[i][0] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("Unbalanced menu braces")


def menu_block(text: str, name: str):
    ts = list(tokens(text)); found = []
    for i, token in enumerate(ts[:-2]):
        if token[0].lower() != "menudef" or ts[i + 1][0] != "{":
            continue
        end = close_brace(ts, i + 1)
        for j in range(i + 2, min(end, i + 15)):
            if ts[j][0].lower() == "name" and ts[j + 1][0].strip('"') == name:
                found.append((ts[i + 1][1], ts[end][1]))
                break
    if len(found) != 1:
        raise ValueError(f"Expected exactly one menuDef named {name}; found {len(found)}")
    return found[0]


def read_originals(game: Path):
    result = {}; origins = {}
    archives = sorted((game / "main").glob("*.iwd"))
    if not archives:
        raise ValueError("No installed main/*.iwd archives found")
    for archive in archives:
        with zipfile.ZipFile(archive) as source:
            for member in EXPECTED:
                if member not in source.namelist():
                    continue
                if member in result:
                    raise ValueError(f"Ambiguous original UI: {member} appears in more than one archive")
                data = source.read(member)
                actual = sha(data)
                if actual != EXPECTED[member]:
                    raise ValueError(f"Unsupported original {member} SHA-256 {actual}; expected {EXPECTED[member]}")
                result[member] = data
                origins[member] = {"archive": archive.name, "sha256": actual, "bytes": len(data)}
    missing = set(EXPECTED) - result.keys()
    if missing:
        raise ValueError("Original menu files missing: " + ", ".join(sorted(missing)))
    return result, origins


def generated_files(originals, assets: Path):
    main = originals["ui/main.menu"].decode("cp1252")
    loader = originals["ui/menus.txt"].decode("cp1252")
    entry = (assets / "coop_entry.menu").read_text(encoding="ascii")
    own = (assets / "coop.menu").read_bytes()
    own_text = own.decode("ascii")
    for name in MENUS:
        menu_block(own_text, name)
    if "cod2coop_main_entry" not in entry or "open coop_main" not in entry:
        raise ValueError("Independent entry asset does not contain the required co-op action")
    if "cod2coop_main_entry" in main or '"ui/coop.menu"' in loader:
        raise ValueError("Original UI already contains co-op anchors unexpectedly")
    _, end = menu_block(main, "main_text")
    main = main[:end] + f"\r\n// {MARKER}\r\n" + entry + "\r\n" + main[end:]
    ts = list(tokens(loader))
    if not ts or ts[0][0] != "{":
        raise ValueError("Original menus.txt does not have the expected root block")
    end = close_brace(ts, 0)
    if end != len(ts) - 1:
        raise ValueError("Unexpected extra root content in original menus.txt")
    insertion = f'\r\n    // {MARKER}\r\n    loadMenu {{ "ui/coop.menu" }}\r\n'
    loader = loader[:ts[end][1]] + insertion + loader[ts[end][1]:]
    return {"ui/main.menu": main.encode("cp1252"), "ui/menus.txt": loader.encode("cp1252"), "ui/coop.menu": own}


def atomic_write(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name); output.write(data)
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def prior_manifest(stage: Path):
    path = stage / MANIFEST
    if not path.exists():
        return None
    prior = json.loads(path.read_text(encoding="utf-8"))
    if prior.get("generator") != "COD2 CO-OP native UI 0.1.0":
        raise ValueError("Existing UI manifest is not owned by this staging tool")
    return prior


def validate_owned_output(stage: Path, files, prior):
    old = (prior or {}).get("generated", {})
    for name, data in files.items():
        path = stage / name
        resolved = path.resolve()
        if stage not in resolved.parents:
            raise ValueError(f"Output leaves the isolated staging directory: {path}")
        if not path.exists():
            continue
        current = sha(path.read_bytes())
        if current != sha(data) and current != old.get(name, {}).get("sha256"):
            raise ValueError(f"Refusing to overwrite unrelated or edited UI override: {path}")


def remove_generated(stage: Path, dry_run: bool):
    prior = prior_manifest(stage)
    if not prior:
        raise ValueError("No owned UI staging manifest found")
    allowed = {"ui/main.menu", "ui/menus.txt", "ui/coop.menu"}
    if set(prior.get("generated", {})) != allowed:
        raise ValueError("Unexpected generated-file list in UI manifest")
    for name, info in prior["generated"].items():
        path = stage / name
        if stage not in path.resolve().parents:
            raise ValueError(f"Output leaves the isolated staging directory: {path}")
        if path.exists() and sha(path.read_bytes()) != info["sha256"]:
            raise ValueError(f"Refusing to delete an edited staged menu: {path}")
    if not dry_run:
        for name in allowed:
            path = stage / name
            if path.exists():
                path.unlink()
        (stage / MANIFEST).unlink()
    return {"removed": not dry_run, "dry_run": dry_run, "stage": str(stage), "files": sorted(allowed)}


def run(args):
    home = args.home.resolve(); stage = (home / "cod2coop").resolve()
    game = (args.game or discover_game()).resolve()
    if stage == game or game in stage.parents:
        raise ValueError("UI output must be outside the original Steam installation")
    if args.remove:
        return remove_generated(stage, args.dry_run)
    assets = (args.assets or Path(__file__).resolve().parents[1] / "assets/original-mod-assets-only/ui").resolve()
    originals, origins = read_originals(game)
    files = generated_files(originals, assets)
    prior = prior_manifest(stage)
    validate_owned_output(stage, files, prior)
    manifest = {
        "generator": "COD2 CO-OP native UI 0.1.0",
        "branding": "COD2 CO-OP — ALPHA 0.1.0; Developed by Heperkoo",
        "game": str(game), "home": str(home), "stage": str(stage),
        "originals": origins,
        "generated": {name: {"sha256": sha(data), "bytes": len(data), "contains_original_game_content": name != "ui/coop.menu"} for name, data in files.items()},
        "menu_names": sorted(MENUS),
        "native_tabs": ["main", "host", "join", "soldier", "settings"],
        "runtime_commands_required": ["coop_host", "coop_join", "coop_start", "coop_disconnect", "coop_soldier"],
        "dry_run": args.dry_run,
        "warning": "Generated main.menu and menus.txt are local derivatives of installed assets; exclude them from redistribution.",
    }
    if not args.dry_run:
        for name, data in files.items():
            atomic_write(stage / name, data)
        atomic_write(stage / MANIFEST, (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, help="Original Steam Call of Duty 2 directory")
    parser.add_argument("--home", type=Path, required=True, help="Isolated fs_homepath; generated overrides go in its cod2coop/ui subdirectory")
    parser.add_argument("--assets", type=Path, help="Directory containing independently authored coop.menu and coop_entry.menu")
    parser.add_argument("--dry-run", action="store_true", help="Verify sources/anchors/ownership and report planned outputs without writing")
    parser.add_argument("--remove", action="store_true", help="Remove only hash-matching generated UI files; preserve profiles, saves and other files")
    try:
        result = run(parser.parse_args()); print(json.dumps(result, indent=2, ensure_ascii=True)); return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"COD2 CO-OP UI staging: {error}", file=sys.stderr); return 1


if __name__ == "__main__":
    raise SystemExit(main())
