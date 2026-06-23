# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-06-23

First release of this fork of
[olokobayusuf/kokoro.cpp](https://github.com/olokobayusuf/kokoro.cpp), adding a
CLI, an OpenAI-compatible server, cross-platform static builds, and a
Hugging Face based weights workflow.

### Added
- **`kokoro-cli`** — one-shot text-to-speech to a WAV file (parakeet-cli / llama-cli style).
- **`kokoro-server`** — OpenAI-compatible TTS HTTP server:
  - `GET /health` readiness probe.
  - `GET /v1/voices` voice discovery.
  - `POST /v1/audio/speech` synthesis returning `audio/wav`.
- **`include/kokoro_common.hpp`** — shared engine wrapper, voice list, and PCM16 WAV encoder.
- Cross-platform **static ONNX Runtime** selection in CMake (Linux x64/aarch64/arm,
  Windows x64/x86/arm64, macOS universal2), producing standalone binaries.
- **Hugging Face weights workflow**: `scripts/upload-weights-hf.sh` and
  `scripts/download-weights-hf.sh`, with reference weights published at
  [rleo/kokoro-onnx](https://huggingface.co/rleo/kokoro-onnx). The uploader also
  generates a model card carrying the upstream attribution.
- `.gitignore`, project documentation (root + `scripts/` READMEs with auth and
  troubleshooting guidance), badges, and a "forked from" attribution.

### Changed
- Reorganized the repository into `include/`, `src/`, `docs/`, and `scripts/`;
  binaries now build into `bin/`.
- Guarded the unfinished Linux x86_64 CUDA path behind `ORT_CUDA_EP_HASH` so the
  CPU build compiles cleanly.
- **Model weights are no longer stored in git.** Removed the Git LFS tracking and
  the committed `resources/` files; weights are fetched from Hugging Face at
  runtime (and under zallama, downloaded automatically).

### Removed
- `example.cpp` and its build target (superseded by `kokoro-cli`).
- The now-unused AudioFile and stb dependencies.

[0.1.0]: https://github.com/rzafiamy/kokoro.cpp/releases/tag/v0.1.0
