# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for EagleGD (Linux AppImage payload)

from pathlib import Path

from PyInstaller.utils.hooks import collect_all

block_cipher = None
ROOT = Path(SPECPATH).resolve().parent

datas = [
    (str(ROOT / "resources"), "resources"),
    (str(ROOT / "model"), "model"),
]

binaries = []
hiddenimports = [
    "eaglegd",
    "eaglegd.app.main",
    "eaglegd.app.Application",
    "eaglegd.ui.MainWindow",
    "eaglegd.ui.ChatPanel",
    "eaglegd.llm.OllamaBackend",
    "eaglegd.stt.stt_engine",
    "eaglegd.tts.SpeechQueuePlayer",
    "eaglegd.curriculum.curriculum_controller",
]

# Pull PySide6 + shiboken runtime files
for pkg in ("PySide6", "shiboken6"):
    pkg_datas, pkg_binaries, pkg_hidden = collect_all(pkg)
    datas += pkg_datas
    binaries += pkg_binaries
    hiddenimports += pkg_hidden

a = Analysis(
    [str(ROOT / "eaglegd" / "app" / "main.py")],
    pathex=[str(ROOT)],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="EagleGD",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="EagleGD",
)
