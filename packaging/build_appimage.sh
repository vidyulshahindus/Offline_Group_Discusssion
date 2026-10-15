#!/usr/bin/env bash
# Build EagleGD.AppImage and copy it (+ companion files) to ~/Downloads/EagleGD_AppImage/
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${EAGLEGD_APPIMAGE_OUT:-$HOME/Downloads/EagleGD_AppImage}"
BUILD_DIR="$ROOT/packaging/build"
APPDIR="$BUILD_DIR/EagleGD.AppDir"
DIST_DIR="$BUILD_DIR/dist"

echo "==> Root: $ROOT"
echo "==> Output: $OUT_DIR"

mkdir -p "$BUILD_DIR" "$OUT_DIR"
cd "$ROOT"

if [[ ! -d "$ROOT/.venv" ]]; then
  python3 -m venv "$ROOT/.venv"
fi
# shellcheck disable=SC1091
source "$ROOT/.venv/bin/activate"
pip install -q --upgrade pip wheel
pip install -q -r "$ROOT/requirements.txt"
pip install -q -e "$ROOT"
pip install -q "pyinstaller>=6.0"

echo "==> PyInstaller"
rm -rf "$DIST_DIR" "$BUILD_DIR/pyinstaller"
pyinstaller \
  --noconfirm \
  --clean \
  --distpath "$DIST_DIR" \
  --workpath "$BUILD_DIR/pyinstaller" \
  "$ROOT/packaging/eaglegd.spec"

echo "==> Assemble AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/EagleGD" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# PyInstaller payload
cp -a "$DIST_DIR/EagleGD/." "$APPDIR/usr/bin/"

# Resources + model (also already inside COLLECT, but keep share layout for Paths)
cp -a "$ROOT/resources" "$APPDIR/usr/share/EagleGD/"
cp -a "$ROOT/model" "$APPDIR/usr/share/EagleGD/"

# Speech tools (whisper + piper + voices/models)
if [[ -d "$ROOT/tools" ]]; then
  echo "==> Copying tools/ (speech binaries + models)…"
  mkdir -p "$APPDIR/usr/share/EagleGD/tools"
  cp -a "$ROOT/tools/." "$APPDIR/usr/share/EagleGD/tools/"
  # Also symlink/copy next to binary for simple resolution
  ln -sfn ../share/EagleGD/tools "$APPDIR/usr/bin/tools" || \
    cp -a "$ROOT/tools" "$APPDIR/usr/bin/tools"
fi

# Wrapper so AppImage sets APPDIR-friendly cwd and library path
cat > "$APPDIR/AppRun" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="${APPDIR:-$HERE}"
export PATH="$HERE/usr/bin:$PATH"
# Prefer bundled libs if present
if [[ -d "$HERE/usr/lib" ]]; then
  export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
fi
# Qt plugins from PySide6 bundle
for plug in "$HERE"/usr/bin/PySide6/Qt/plugins "$HERE"/usr/bin/_internal/PySide6/Qt/plugins; do
  if [[ -d "$plug" ]]; then
    export QT_PLUGIN_PATH="$plug"
    break
  fi
done
exec "$HERE/usr/bin/EagleGD" "$@"
EOF
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/eagle-gd.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Eagle GD
Comment=Eagle Group Discussion offline classroom assistant
Exec=EagleGD
Icon=eagle-gd
Categories=Education;AudioVideo;
Terminal=false
EOF
cp "$APPDIR/eagle-gd.desktop" "$APPDIR/usr/share/applications/"

# Simple placeholder icon (PNG) — generate with Python if pillow missing use PPM convert skip
python3 - << 'PY'
from pathlib import Path
try:
    from PIL import Image, ImageDraw, ImageFont
    img = Image.new("RGBA", (256, 256), (20, 90, 160, 255))
    d = ImageDraw.Draw(img)
    d.ellipse((40, 40, 216, 216), fill=(240, 248, 255, 255))
    d.text((88, 100), "EG", fill=(20, 90, 160, 255))
    out = Path("packaging/build/EagleGD.AppDir/usr/share/icons/hicolor/256x256/apps/eagle-gd.png")
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    # AppDir root icon
    img.save(Path("packaging/build/EagleGD.AppDir/eagle-gd.png"))
    print("icon ok")
except Exception as e:
    # Minimal valid 1x1 PNG
    import struct, zlib
    def png(w,h,rgb=(30,100,180)):
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xffffffff)
        raw = b"".join(b"\x00" + bytes(rgb)*w for _ in range(h))
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w,h,8,2,0,0,0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    p = Path("packaging/build/EagleGD.AppDir/eagle-gd.png")
    p.write_bytes(png(256,256))
    Path("packaging/build/EagleGD.AppDir/usr/share/icons/hicolor/256x256/apps/eagle-gd.png").write_bytes(p.read_bytes())
    print("icon fallback", e)
PY

echo "==> Fetch appimagetool"
TOOL="$BUILD_DIR/appimagetool-x86_64.AppImage"
if [[ ! -x "$TOOL" ]]; then
  curl -L --fail -o "$TOOL" \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
  chmod +x "$TOOL"
fi

echo "==> Build AppImage"
cd "$BUILD_DIR"
export ARCH=x86_64
# Extract appimagetool if FUSE unavailable
if ! "$TOOL" --appimage-help >/dev/null 2>&1; then
  echo "Running appimagetool via extract-and-run"
  APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" "$APPDIR" "$OUT_DIR/EagleGD-x86_64.AppImage"
else
  "$TOOL" "$APPDIR" "$OUT_DIR/EagleGD-x86_64.AppImage" || \
    APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" "$APPDIR" "$OUT_DIR/EagleGD-x86_64.AppImage"
fi

chmod +x "$OUT_DIR/EagleGD-x86_64.AppImage" || true

# Companion config sample + README
cp -a "$ROOT/resources/config/eagle_config.defaults.json" "$OUT_DIR/eagle_config.sample.json"
cp -a "$ROOT/model/Modelfile.eagle-clm" "$OUT_DIR/Modelfile.eagle-clm"

cat > "$OUT_DIR/README_INSTALL.txt" << 'EOF'
Eagle GD — AppImage install notes (Linux)
=========================================

1) Make executable
   chmod +x EagleGD-x86_64.AppImage

2) Run
   ./EagleGD-x86_64.AppImage

   If you see a FUSE error:
   ./EagleGD-x86_64.AppImage --appimage-extract-and-run
   # or:
   ./EagleGD-x86_64.AppImage --appimage-extract
   ./squashfs-root/AppRun

3) REQUIRED on the other PC (not inside the AppImage)
   - Ollama installed and running: https://ollama.com/download
   - Create the model once:

       ollama pull qwen3.5:4b-q4_K_M
       ollama create eagle-clm -f Modelfile.eagle-clm
       ollama list

   Ollama does NOT need an API key for local use.
   It listens on http://127.0.0.1:11434 by default.

4) Where to change Ollama URL / model (if needed)
   Config file (created on first run):

     ~/.config/IndusInternational/Eagle GD/eagle_config.json

   Or copy eagle_config.sample.json there and edit:

     "llm": {
       "base_url": "http://127.0.0.1:11434",
       "model": "eagle-clm",
       "fallback_model": "qwen3.5:2b-q4_K_M"
     }

   Or launch with an explicit config:

     ./EagleGD-x86_64.AppImage --config /path/to/eagle_config.json

   There is normally NO cloud API key. If you ever point base_url at a
   remote Ollama gateway that requires a token, that would be configured
   outside this AppImage (not a standard Eagle GD setting).

5) Optional system packages (Ubuntu/Debian) if the window fails to open
   sudo apt update
   sudo apt install libxcb-cursor0 libxkbcommon0 libegl1 libgl1 libpulse0 libportaudio2 fuse3

6) Speech tools
   whisper-server + Piper voices are bundled inside the AppImage under
   usr/share/EagleGD/tools. No separate tools install is required unless
   you want to override paths in config (paths.tools_dir).

7) First launch
   Start Ollama, then run the AppImage. Open Readiness (Ctrl+Shift+R)
   to confirm Ollama / STT / TTS.
EOF

echo ""
echo "Done."
ls -lh "$OUT_DIR"
echo "AppImage folder: $OUT_DIR"
