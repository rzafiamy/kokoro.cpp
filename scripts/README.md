# scripts/

Helper scripts for managing the Kokoro **model weights**, which live in a
Hugging Face model repo rather than in git (see the root
[README](../README.md#model-weights)).

> [!NOTE]
> Both scripts use the `hf` CLI (`pip install huggingface_hub`). Uploading needs
> authentication (see below); downloading public weights does not.
>
> Reference weights are already published at
> **[huggingface.co/rleo/kokoro-onnx](https://huggingface.co/rleo/kokoro-onnx)**.

## Scripts

| Script | Purpose |
| --- | --- |
| [`upload-weights-hf.sh`](upload-weights-hf.sh) | Publish your local `resources/` to a Hugging Face model repo (one-time). |
| [`download-weights-hf.sh`](download-weights-hf.sh) | Fetch the weights from Hugging Face into a local directory for a run. |

## Authentication (upload only)

Uploading requires a logged-in `hf` CLI with **write** rights:

```sh
# 1. Create a token at https://huggingface.co/settings/tokens
#    - a "Write" token works out of the box, OR
#    - a fine-grained token with "Write access to contents/settings of all
#      repos under your personal namespace"
# 2. Log in (paste the token when prompted)
hf auth login

# 3. Confirm you're authenticated and note your username (the namespace you own)
hf auth whoami
```

> [!IMPORTANT]
> A **fine-grained** token can *write to existing repos* but may not be allowed
> to *create* them. If `upload-weights-hf.sh` reports it could not create the
> repo, create it once in the browser at
> [huggingface.co/new](https://huggingface.co/new) (owner = your username, type =
> **Model**), then re-run the script — the upload step will work. The script is
> written to continue past a create failure for exactly this case.

## Usage

```sh
# Publish your local weights to a repo under YOUR namespace
# (use your own hf username — `hf auth whoami` — not someone else's)
./scripts/upload-weights-hf.sh <your-username>/kokoro-onnx

# Fetch them into ./resources for a local run (no login needed if public)
./scripts/download-weights-hf.sh rleo/kokoro-onnx resources/
```

## The weight files

This build expects a **muna-transpiled** Kokoro: two ONNX models plus a NumPy
voice pack, named by content hash. **Do not rename them** — the loader
([`include/kokoro.kokoro_tts.hpp`](../include/kokoro.kokoro_tts.hpp)) looks them
up by these exact names:

| Filename (hashed) | Role | Size |
| --- | --- | --- |
| `8fbea51ea711…` | acoustic ONNX model | ~311 MB |
| `67f7dd6fed17…` | phonemizer ONNX model | ~47 MB |
| `c3bf79648d4d…` | voices (`.npz`) | ~28 MB |

> [!WARNING]
> Other public Kokoro repos use incompatible layouts and will **not** load with
> this build: `hexgrad/Kokoro-82M` is PyTorch `.pth`; `nvidia/kokoro-82M-onnx-opt`
> is a single ONNX + `voices.bin` + FST phonemizer. Host the three files above.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `401 Unauthorized` on create/upload | Not logged in / no token. | `hf auth login` with a write token; verify with `hf auth whoami`. |
| `403 Forbidden … rights to create … namespace "<x>"` | Token can't create repos (fine-grained), or `<x>` isn't your namespace. | Create the repo at [huggingface.co/new](https://huggingface.co/new), or use `<your-username>/…`. |
| Only one of the three files uploads | Older `hf` CLI handles multi-`--include` poorly. | The script now uploads each file explicitly — re-run it, or push the missing file with `hf upload <repo> resources/<hash> <hash>`. |
| `hf: error: unrecognized arguments: -y` | CLI version uses `--exist-ok`, not `-y`. | Update the script / `hf` CLI (already handled here). |
