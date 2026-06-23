# scripts/

Helper scripts for managing the Kokoro **model weights**, which live in a
Hugging Face model repo rather than in git (see the root
[README](../README.md#model-weights)).

> [!NOTE]
> Both scripts use the `hf` CLI (`pip install huggingface_hub`). Uploading needs
> `hf auth login`; downloading public weights does not.

## Scripts

| Script | Purpose |
| --- | --- |
| [`upload-weights-hf.sh`](upload-weights-hf.sh) | Publish your local `resources/` to a Hugging Face model repo (one-time). |
| [`download-weights-hf.sh`](download-weights-hf.sh) | Fetch the weights from Hugging Face into a local directory for a run. |

## Usage

```sh
# Publish your local weights (creates the repo if needed)
./scripts/upload-weights-hf.sh <user>/kokoro-onnx

# Fetch them into ./resources for a local run
./scripts/download-weights-hf.sh <user>/kokoro-onnx resources/
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
