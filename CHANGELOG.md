# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **espeak-ng G2P backend** (`include/kokoro_espeak.hpp`): when libespeak-ng is
  installed, phonemization uses it instead of the bundled autoregressive ByT5
  model, which costs ~50 ms per generated phoneme. A 1400-char French text
  drops from ~57 s to ~30 s of synthesis (the remainder is the acoustic model);
  espeak also emits the stress marks the ByT5 never produced. The library is
  loaded at runtime via `dlopen`/`LoadLibrary` — no new build dependency — and
  everything falls back to the ByT5 automatically (missing library, unsupported
  language such as ja/zh, or per-clause failure). Opt out with `--no-espeak`
  (both `kokoro-cli` and `kokoro-server`); both binaries log which phonemizer
  is active.

### Fixed
- **Non-English voices no longer speak with English phonemes.** When no
  `language` is given, it is now inferred from the voice prefix
  (`ff_siwis` → `fr-FR`, `if_sara` → `it-IT`, …) instead of always falling back
  to `en-US`, which phonemized French text with English sounds
  ("Ensuite" → `ɛnsuːtiː` instead of `ɑ̃syit`).
- **Typographic characters are normalized before phonemization.** The curly
  apostrophe `’` garbled every French elision ("l’idée" was spoken as
  "le ouide"); `’ ‘ “ ” « »`, en dash, and no-break spaces are now mapped to
  ASCII equivalents.
- **Punctuation now survives phonemization, restoring intonation.** The ByT5
  phonemizer drops `, . ! ?` from its IPA output and even hallucinates phonemes
  for them ("Et vous ?" came back as "e vu ɛl"), so the synthesizer never saw
  the prosody marks it was trained on. Text is now phonemized clause by clause
  and the punctuation re-attached to the IPA stream (first hand-written patch
  inside `kokoro.kokoro_tts.hpp`, clearly marked). Also ~15% faster on long
  inputs since the phonemizer sees shorter sequences.
- **French numbers are written out before phonemization** ("250" was spoken as
  "tefach", "2024" as "tetfa"): cardinals, decimals ("3,14"), thousands
  grouping ("1.000"), `% $ €`, ordinals ("1er", "21ème"), and ALL-CAPS
  acronyms ("IA" → "i a", including behind an elision "d'IA").

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
