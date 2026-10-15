from __future__ import annotations

import os
import sys
from pathlib import Path


def _appimage_dir() -> Path | None:
    appdir = os.environ.get("APPDIR")
    if appdir:
        return Path(appdir)
    return None


def repo_root() -> Path:
    """Project / install root (resources live under this)."""
    # PyInstaller one-folder / one-file
    if getattr(sys, "frozen", False):
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            # Prefer bundled resources inside the extract dir.
            candidate = Path(meipass)
            if (candidate / "resources").is_dir():
                return candidate
        exe_dir = Path(sys.executable).resolve().parent
        # AppImage: .../usr/bin/EagleGD → share next to usr
        for base in (
            exe_dir,
            exe_dir.parent,
            exe_dir.parent / "share" / "EagleGD",
            exe_dir.parent.parent / "share" / "EagleGD",
        ):
            if (base / "resources").is_dir():
                return base
        appdir = _appimage_dir()
        if appdir is not None:
            for base in (
                appdir / "usr" / "share" / "EagleGD",
                appdir,
            ):
                if (base / "resources").is_dir():
                    return base
        return exe_dir

    return Path(__file__).resolve().parent.parent


def resource_path(*parts: str) -> Path:
    return repo_root().joinpath("resources", *parts)


def tools_search_roots() -> list[Path]:
    """Directories to search for whisper/piper tools."""
    roots: list[Path] = []
    root = repo_root()
    roots.append(root / "tools")
    if getattr(sys, "frozen", False):
        exe_dir = Path(sys.executable).resolve().parent
        roots.extend(
            [
                exe_dir / "tools",
                exe_dir.parent / "tools",
                exe_dir.parent / "share" / "EagleGD" / "tools",
            ]
        )
        appdir = _appimage_dir()
        if appdir is not None:
            roots.append(appdir / "usr" / "share" / "EagleGD" / "tools")
            roots.append(appdir / "tools")
    # Deduplicate while preserving order
    seen: set[str] = set()
    out: list[Path] = []
    for path in roots:
        key = str(path.resolve()) if path.exists() else str(path)
        if key not in seen:
            seen.add(key)
            out.append(path)
    return out
