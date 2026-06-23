#!/usr/bin/env bash
# upload-weights-hf.sh
# Upload the local kokoro model weights (resources/) to a Hugging Face model
# repo so they can be downloaded at runtime instead of living in git.
#
# These are the muna-transpiled ONNX/npz artifacts this build expects, named by
# content hash — DO NOT rename them, the loader looks them up by these names:
#   8fbea51ea711...  acoustic ONNX model   (~311 MB)
#   67f7dd6fed17...  phonemizer ONNX model (~47 MB)
#   c3bf79648d4d...  voices (.npz)         (~28 MB)
#
# Usage:
#   ./scripts/upload-weights-hf.sh <hf-username>/<repo-name>
# Example:
#   ./scripts/upload-weights-hf.sh rzafiamy/kokoro-onnx
#
# Requires: the `hf` CLI (pip install huggingface_hub) and a prior `hf auth login`.

set -euo pipefail

REPO="${1:-}"
if [ -z "$REPO" ]; then
    echo "usage: $0 <hf-username>/<repo-name>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RES_DIR="${SCRIPT_DIR}/../resources"

# The exact files this build loads (see include/kokoro.kokoro_tts.hpp).
FILES=(
    "8fbea51ea711f2af382e88c833d9e288c6dc82ce5e98421ea61c058ce21a34cb"
    "67f7dd6fed1742193e475a2fe9d060df315d9a6f434b966b15aec69fc2ed966c"
    "c3bf79648d4d8b7874b992e1cc6275688e7881f0818ec6ed8e7255c32d05ba11"
)

for f in "${FILES[@]}"; do
    if [ ! -f "${RES_DIR}/${f}" ]; then
        echo "error: missing weight file ${RES_DIR}/${f}" >&2
        echo "       (resources/ is git-ignored — make sure the weights are present locally)" >&2
        exit 1
    fi
done

echo "📦 Creating repo (if needed): ${REPO}"
hf repo create "${REPO}" --repo-type model -y >/dev/null 2>&1 || true

# Generate a model card (README.md) carrying the attribution/disclaimer. Written
# to a temp file so nothing is left behind in the working tree.
CARD="$(mktemp --suffix=.md)"
trap 'rm -f "${CARD}"' EXIT
cat > "${CARD}" <<'CARD_EOF'
---
license: apache-2.0
library_name: onnx
pipeline_tag: text-to-speech
tags:
  - kokoro
  - tts
  - onnx
  - kokoro.cpp
---

# Kokoro TTS — ONNX weights for kokoro.cpp

ONNX/npz model weights consumed by
[rzafiamy/kokoro.cpp](https://github.com/rzafiamy/kokoro.cpp) — the C++ build that
adds `kokoro-cli` and the OpenAI-compatible `kokoro-server`.
These are the muna-transpiled artifacts, named by content hash — the loader
looks them up by these exact filenames, so **do not rename them**:

| Filename (hashed) | Role | Size |
| --- | --- | --- |
| `8fbea51ea711…` | acoustic ONNX model | ~311 MB |
| `67f7dd6fed17…` | phonemizer ONNX model | ~47 MB |
| `c3bf79648d4d…` | voices (`.npz`) | ~28 MB |

## Disclaimer & Attribution

These weights were obtained by **forking
[github.com/olokobayusuf/kokoro.cpp](https://github.com/olokobayusuf/kokoro.cpp)**;
they are redistributed here only to be downloaded at runtime (instead of being
committed into git). All credit for the model and the original transpilation goes
to that upstream project and to the underlying
[Kokoro-82M](https://huggingface.co/hexgrad/Kokoro-82M) model.

The `kokoro-cli` / `kokoro-server` build that consumes these files is maintained
in [rzafiamy/kokoro.cpp](https://github.com/rzafiamy/kokoro.cpp). The files are
provided as-is, with no additional claims.

## Usage

```sh
kokoro-server --model <download-dir> --host 127.0.0.1 --port 8080
```
CARD_EOF

echo "⬆️  Uploading model card + ${#FILES[@]} weight files to ${REPO}..."
hf upload "${REPO}" "${CARD}" README.md --repo-type model
hf upload "${REPO}" "${RES_DIR}" . \
    --repo-type model \
    --include "8fbea51ea711*" \
    --include "67f7dd6fed17*" \
    --include "c3bf79648d4d*"

echo "✅ Done. Weights live at: https://huggingface.co/${REPO}"
echo "   Download them into a local dir and run:"
echo "     kokoro-server --model <that-dir> --host 127.0.0.1 --port 8080"
