<h1 align="center">kokoro.cpp</h1>

<p align="center">
  <em>Kokoro-TTS as a single-header C++ library, a CLI, and an OpenAI-compatible HTTP server.</em>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/build-CMake%203.28%2B-064F8C?logo=cmake&logoColor=white">
  <img alt="ONNX Runtime" src="https://img.shields.io/badge/runtime-ONNX%201.20.1-005CED?logo=onnx&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-444">
  <img alt="License" src="https://img.shields.io/badge/license-see%20LICENSE-green">
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#quick-start">Quick Start</a> ·
  <a href="#kokoro-cli">CLI</a> ·
  <a href="#kokoro-server">Server</a> ·
  <a href="#zallama-integration">zallama</a> ·
  <a href="#how-this-was-generated">Provenance</a>
</p>

https://github.com/user-attachments/assets/d644129b-45f6-4652-9956-cfc1f48aefa0

---

## Features

- 🗣️ **Kokoro-TTS** — 50+ voices across multiple languages, 24 kHz mono output.
- 📦 **Single-header library** — `#include "kokoro.kokoro_tts.hpp"`; everything else is fetched by CMake.
- 🧰 **`kokoro-cli`** — one-shot text → WAV file (llama-cli / parakeet-cli style).
- 🌐 **`kokoro-server`** — OpenAI-compatible `POST /v1/audio/speech` over HTTP.
- 🔗 **Standalone binaries** — ONNX Runtime, zlib, simdutf, and cpp-httplib are statically linked. No third-party shared libraries to ship.
- 🧩 **[zallama](#zallama-integration)-ready** — the server speaks the same contract as `llama-server` / `parakeet-server`.

## Layout

```
kokoro.cpp/
├── include/                     # the single-header library + shared helpers
│   ├── kokoro.kokoro_tts.hpp    #   transpiled Kokoro model
│   └── kokoro_common.hpp        #   engine wrapper, voice list, WAV encoder
├── src/
│   ├── cli.cpp                  # kokoro-cli
│   └── server.cpp               # kokoro-server
├── resources/                   # model weights — git-ignored, fetched from HF
├── docs/kokoro.py               # original Python source (provenance)
├── CMakeLists.txt
└── bin/                         # build output (git-ignored)
```

## Quick Start

> [!NOTE]
> Requires a C++20 toolchain, CMake ≥ 3.28, and Git (CMake fetches dependencies at configure time).

```sh
git clone https://github.com/rzafiamy/kokoro.cpp.git
cd kokoro.cpp

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

> [!IMPORTANT]
> **Model weights are not in this repo.** They live in a Hugging Face model
> repo and are downloaded separately into a local directory, which you pass via
> `--model <dir>`. The directory must contain the model files under their
> original (hashed) filenames — the loader looks them up by those names. Under
> [zallama](#zallama-integration) this download is handled for you.

### Model weights

This build expects a **muna-transpiled** Kokoro: two ONNX models plus a NumPy
voice pack, named by content hash. Other public Kokoro repos use incompatible
layouts (`hexgrad/Kokoro-82M` is PyTorch `.pth`; `nvidia/kokoro-82M-onnx-opt` is
a single ONNX + `voices.bin` + FST phonemizer), so host **these exact files**:

| Filename (hashed) | Role | Size |
| --- | --- | --- |
| `8fbea51ea711…` | acoustic ONNX model | ~311 MB |
| `67f7dd6fed17…` | phonemizer ONNX model | ~47 MB |
| `c3bf79648d4d…` | voices (`.npz`) | ~28 MB |

```sh
# Publish your local weights to a Hugging Face model repo (one-time)
./scripts/upload-weights-hf.sh <user>/kokoro-onnx

# Fetch them into ./resources for a local run
./scripts/download-weights-hf.sh <user>/kokoro-onnx resources/
```

Binaries are written to `./bin`:

| Binary | Purpose |
| --- | --- |
| `bin/kokoro-cli` | One-shot text-to-speech to a WAV file. |
| `bin/kokoro-server` | OpenAI-compatible TTS HTTP server. |

### Build options

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_CLI` | `ON` | Build `kokoro-cli`. |
| `BUILD_SERVER` | `ON` | Build `kokoro-server` (pulls in cpp-httplib). |

## kokoro-cli

```sh
# List the available voices
./bin/kokoro-cli --list-voices

# Synthesize to a WAV file
./bin/kokoro-cli \
  --model resources/ \
  --text "Hello from kokoro." \
  --voice af_heart \
  --output hello.wav
```

| Flag | Default | Description |
| --- | --- | --- |
| `-m, --model` | `resources/` | Resource directory with the model files. |
| `-t, --text` | *(required)* | Text to synthesize. |
| `-v, --voice` | `af_heart` | Voice id (see `--list-voices`). |
| `-l, --language` | *(model default)* | Language override. |
| `-s, --speed` | `1.0` | Speaking speed multiplier. |
| `-o, --output` | `output.wav` | Output WAV path. |
| `--list-voices` | | Print available voices and exit. |

## kokoro-server

```sh
./bin/kokoro-server --model resources/ --host 127.0.0.1 --port 8080
```

| Flag | Default | Description |
| --- | --- | --- |
| `-m, --model` | `resources/` | Resource directory with the model files. |
| `--host` | `127.0.0.1` | Interface to bind. |
| `-p, --port` | `8080` | Port to listen on. |

### Endpoints

| Method | Path | Description |
| --- | --- | --- |
| `GET` | `/health` | Readiness probe → `{"status":"ok"}`. |
| `GET` | `/v1/voices` | List available voices. |
| `POST` | `/v1/audio/speech` | OpenAI-compatible synthesis → `audio/wav`. |

```sh
curl -s http://127.0.0.1:8080/v1/audio/speech \
  -H "Content-Type: application/json" \
  -d '{"input":"This is the kokoro server.","voice":"am_adam","speed":1.0}' \
  -o speech.wav
```

Request body (`POST /v1/audio/speech`):

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `input` | string | ✅ | Text to synthesize. |
| `voice` | string | | Voice id (default `af_heart`). |
| `speed` | number | | Speaking speed multiplier. |
| `language` | string | | Language override. |
| `response_format` | string | | `wav`/`pcm` honoured; others fall back to WAV. |

## zallama Integration

`kokoro-server` follows the same launch + HTTP contract as `llama-server` and
`parakeet-server`, so it plugs into [zallama](https://github.com/rzafiamy) as a
`tts` backend:

- zallama downloads the weights from Hugging Face into a local directory
- launched as `kokoro-server --model <dir> --host 127.0.0.1 --port <port>`
- health-checked on `GET /health`
- served via `POST /v1/audio/speech`

> [!TIP]
> kokoro's `--model` is a **resource directory** (not a single weights file),
> because `muna::Configuration` loads every file in that directory as a manifest.
> The server itself does **no downloading** — it only reads a local path, which
> keeps the binary fully standalone (no TLS/HTTP-client dependency).

## How This Was Generated

The single-header library is **transpiled** from [`docs/kokoro.py`](docs/kokoro.py)
to C++ with [`muna transpile`](https://github.com/muna-ai/muna-py):

```sh
pip install muna && muna transpile docs/kokoro.py --install-deps
```

To deploy across platforms, see [`muna compile`](https://docs.muna.ai/predictors/create),
which targets cloud and local hardware behind an OpenAI-style client.

## Acknowledgements

- [Kokoro-TTS](https://github.com/hexgrad/kokoro) — the underlying model.
- [Muna](https://github.com/muna-ai/muna-py) — Python → C++ transpilation. [Join the Slack](https://muna.ai/slack) · [Docs](https://docs.muna.ai).
- [cpp-httplib](https://github.com/yhirose/cpp-httplib), [ONNX Runtime](https://onnxruntime.ai), and the rest of the CMake-fetched stack.
