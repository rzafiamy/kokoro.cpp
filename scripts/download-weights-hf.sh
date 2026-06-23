#!/usr/bin/env bash
# download-weights-hf.sh
# Fetch the kokoro weights from a Hugging Face model repo into a local directory,
# under the exact (hash) filenames the loader expects. This mirrors what zallama
# does automatically; use it for standalone/manual runs.
#
# Usage:
#   ./scripts/download-weights-hf.sh <hf-username>/<repo-name> [dest-dir]
# Example:
#   ./scripts/download-weights-hf.sh rzafiamy/kokoro-onnx resources/
#
# Requires the `hf` CLI (pip install huggingface_hub). The files are public, so
# no login is needed unless the repo is private.

set -euo pipefail

REPO="${1:-}"
DEST="${2:-resources}"
if [ -z "$REPO" ]; then
    echo "usage: $0 <hf-username>/<repo-name> [dest-dir]" >&2
    exit 2
fi

mkdir -p "$DEST"
echo "⬇️  Downloading weights from ${REPO} into ${DEST}/ ..."
hf download "${REPO}" \
    8fbea51ea711f2af382e88c833d9e288c6dc82ce5e98421ea61c058ce21a34cb \
    67f7dd6fed1742193e475a2fe9d060df315d9a6f434b966b15aec69fc2ed966c \
    c3bf79648d4d8b7874b992e1cc6275688e7881f0818ec6ed8e7255c32d05ba11 \
    --repo-type model \
    --local-dir "$DEST"

echo "✅ Weights ready in ${DEST}/"
echo "   Run: kokoro-server --model ${DEST} --host 127.0.0.1 --port 8080"
