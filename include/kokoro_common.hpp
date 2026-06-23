// kokoro_common.hpp
// Shared helpers for kokoro-cli and kokoro-server: a thin engine wrapper around
// kokoro::kokoro_tts plus minimal WAV (PCM16) encoding. Kept header-only and
// dependency-light so both executables stay statically linkable.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "kokoro.kokoro_tts.hpp"

namespace kokoro_common {

// Kokoro always synthesizes mono audio at 24 kHz.
inline constexpr int kSampleRate = 24000;
inline constexpr int kNumChannels = 1;

// Synthesis runs two model stages per call; the first is an autoregressive,
// per-character phonemizer with no KV cache, so its cost grows ~quadratically
// with input length. Feeding it one large block is what makes long inputs crawl
// (a ~430-char poem ≈ 50s). We instead split into sentence-sized chunks, run the
// pipeline per chunk, and concatenate the PCM: ~N small O(k²) runs instead of one
// O(N²) run. kMaxChunkChars caps a single chunk so a run-on line still gets cut;
// kChunkGapMs is the silence inserted between chunks so boundaries don't click.
inline constexpr size_t kMaxChunkChars = 200;
inline constexpr int kChunkGapMs = 60;

// Strip characters the synthesizer can't speak and collapse whitespace. Callers
// often pass markdown (e.g. "**Risk**:", "*   bullet"), and the tokenizer
// silently drops unknown characters mid-word, producing garbled phonemes at the
// edges. We keep letters, digits, whitespace, and the punctuation that carries
// prosody (.,!?;:'"-) and drop the rest, then squeeze runs of spaces/newlines.
inline std::string sanitize_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c) || std::isspace(c) || c >= 0x80 /* keep UTF-8 */) {
            out.push_back(static_cast<char>(c));
        } else if (std::strchr(".,!?;:'\"-", c)) {
            out.push_back(static_cast<char>(c));
        } else {
            // Replace a dropped symbol with a space so it can't fuse two words.
            out.push_back(' ');
        }
    }
    // Collapse internal whitespace runs to a single space, keep paragraph breaks
    // as newlines so split_into_chunks can still use them as boundaries.
    std::string squeezed;
    squeezed.reserve(out.size());
    bool pending_newline = false, pending_space = false;
    for (char c : out) {
        if (c == '\n') { pending_newline = true; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { pending_space = true; continue; }
        if (!squeezed.empty()) {
            if (pending_newline) squeezed.push_back('\n');
            else if (pending_space) squeezed.push_back(' ');
        }
        pending_newline = pending_space = false;
        squeezed.push_back(c);
    }
    return squeezed;
}

// Split `text` into chunks no longer than kMaxChunkChars, breaking on sentence
// terminators (.!?) and newlines and keeping the terminator with its sentence.
// A .!? only ends a sentence when followed by whitespace or end-of-text, so
// abbreviations like "e.g." are not fragmented. If a single sentence exceeds the
// cap it is hard-split on whitespace. The input is sanitized first. Returns
// trimmed, non-empty chunks; falls back to the whole (sanitized) text as one.
inline std::vector<std::string> split_into_chunks(
    const std::string& raw_text, size_t max_chars = kMaxChunkChars) {
    const std::string text = sanitize_text(raw_text);
    auto trim = [](std::string s) {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    };
    auto push_capped = [&](std::vector<std::string>& out, std::string sentence) {
        sentence = trim(std::move(sentence));
        if (sentence.empty()) return;
        // Hard-split an over-long sentence on whitespace so no chunk blows the cap.
        while (sentence.size() > max_chars) {
            size_t cut = sentence.rfind(' ', max_chars);
            if (cut == std::string::npos || cut == 0) cut = max_chars;
            out.push_back(trim(sentence.substr(0, cut)));
            sentence = trim(sentence.substr(cut));
        }
        if (!sentence.empty()) out.push_back(std::move(sentence));
    };

    std::vector<std::string> chunks;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        current.push_back(c);
        // A terminator only breaks if it ends the text or is followed by space.
        const bool terminator = c == '.' || c == '!' || c == '?';
        const bool followed_by_space =
            i + 1 >= text.size() ||
            std::isspace(static_cast<unsigned char>(text[i + 1]));
        const bool is_break = c == '\n' || (terminator && followed_by_space);
        if (is_break) {
            push_capped(chunks, current);
            current.clear();
        }
    }
    push_capped(chunks, current);

    if (chunks.empty()) {
        std::string whole = trim(text);
        if (!whole.empty()) chunks.push_back(std::move(whole));
    }
    return chunks;
}

// Voices baked into the model. Kept here so both the CLI (--list-voices) and the
// server (/v1/audio/speech validation, GET /v1/voices) share one source.
inline const std::vector<std::string>& voices() {
    static const std::vector<std::string> kVoices = {
        "af_alloy", "af_aoede", "af_bella", "af_heart", "af_jessica", "af_kore",
        "af_nicole", "af_nova", "af_river", "af_sarah", "af_sky", "am_adam",
        "am_echo", "am_eric", "am_fenrir", "am_liam", "am_michael", "am_onyx",
        "am_puck", "am_santa", "bf_alice", "bf_emma", "bf_isabella", "bf_lily",
        "bm_daniel", "bm_fable", "bm_george", "bm_lewis", "ef_dora", "em_alex",
        "em_santa", "ff_siwis", "hf_alpha", "hf_beta", "hm_omega", "hm_psi",
        "if_sara", "im_nicola", "jf_alpha", "jf_gongitsune", "jf_nezumi",
        "jf_tebukuro", "jm_kumo", "pf_dora", "pm_alex", "pm_santa", "zf_xiaobei",
        "zf_xiaoni", "zf_xiaoxiao", "zf_xiaoyi", "zm_yunjian", "zm_yunxi",
        "zm_yunxia", "zm_yunyang",
    };
    return kVoices;
}

inline bool is_known_voice(const std::string& voice) {
    const auto& v = voices();
    return std::find(v.begin(), v.end(), voice) != v.end();
}

// Engine: loads the kokoro model once from a resource directory and synthesizes
// on demand. Construction is expensive (sessions, voice tables); reuse one
// instance for the lifetime of the process. Not thread-safe — callers that run
// it from multiple request handlers must serialize access.
class Engine {
public:
    explicit Engine(const std::filesystem::path& resource_dir)
        : configuration_(resource_dir), model_(configuration_) {}

    // Synthesize `text` with `voice`, returning mono float samples in [-1, 1] at
    // kSampleRate. `language`/`speed` follow the model defaults when unset.
    //
    // The text is split into sentence-sized chunks (see split_into_chunks) that
    // are synthesized independently and concatenated, with a short silence gap
    // between them. This keeps the per-chunk phonemizer input small and avoids
    // the quadratic blow-up of running one large input through the autoregressive
    // phonemizer. Each chunk starts prosody fresh, which reads naturally for
    // multi-sentence input.
    std::vector<float> synthesize(
        const std::string& text,
        const std::string& voice,
        std::optional<std::string> language = std::nullopt,
        std::optional<float> speed = std::nullopt) {
        const auto chunks = split_into_chunks(text);
        if (chunks.size() <= 1) {
            // Nothing to gain from chunking; avoid the concat/gap overhead.
            return synthesize_one(chunks.empty() ? text : chunks.front(), voice,
                                  language, speed);
        }

        const size_t gap = static_cast<size_t>(kSampleRate) * kChunkGapMs / 1000;
        std::vector<float> samples;
        for (size_t i = 0; i < chunks.size(); ++i) {
            auto part = synthesize_one(chunks[i], voice, language, speed);
            samples.insert(samples.end(), part.begin(), part.end());
            if (i + 1 < chunks.size())
                samples.insert(samples.end(), gap, 0.0f);
        }
        return samples;
    }

private:
    // One pipeline pass over `text`: phonemize + synthesize, no chunking.
    std::vector<float> synthesize_one(
        const std::string& text,
        const std::string& voice,
        std::optional<std::string> language,
        std::optional<float> speed) {
        muna::Tensor out = model_(text, voice, language, speed);
        const auto num_frames = out.shape()[0];
        const auto num_channels = out.dims() > 1 ? out.shape()[1] : 1;
        std::vector<float> samples(static_cast<size_t>(num_frames) * num_channels);
        const float* data = out.data<float>();
        std::copy(data, data + samples.size(), samples.begin());
        return samples;
    }

    muna::Configuration configuration_;
    kokoro::kokoro_tts model_;
};

// Encode mono float samples to a 16-bit PCM WAV byte buffer (RIFF). Returned by
// value so the server can hand it straight to an HTTP response; the CLI writes
// the same bytes to a file.
inline std::vector<uint8_t> encode_wav_pcm16(
    const std::vector<float>& samples,
    int sample_rate = kSampleRate,
    int channels = kNumChannels) {
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * sizeof(int16_t));
    const uint16_t block_align = static_cast<uint16_t>(channels * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_bytes;

    std::vector<uint8_t> buf;
    buf.reserve(44 + data_bytes);

    auto put_str = [&](const char* s) { buf.insert(buf.end(), s, s + 4); };
    auto put_u32 = [&](uint32_t v) {
        buf.push_back(v & 0xff); buf.push_back((v >> 8) & 0xff);
        buf.push_back((v >> 16) & 0xff); buf.push_back((v >> 24) & 0xff);
    };
    auto put_u16 = [&](uint16_t v) {
        buf.push_back(v & 0xff); buf.push_back((v >> 8) & 0xff);
    };

    put_str("RIFF"); put_u32(riff_size); put_str("WAVE");
    put_str("fmt "); put_u32(16); put_u16(1 /* PCM */);
    put_u16(static_cast<uint16_t>(channels)); put_u32(static_cast<uint32_t>(sample_rate));
    put_u32(byte_rate); put_u16(block_align); put_u16(16 /* bits */);
    put_str("data"); put_u32(data_bytes);

    for (float s : samples) {
        float c = std::clamp(s, -1.0f, 1.0f);
        int16_t v = static_cast<int16_t>(c < 0 ? c * 32768.0f : c * 32767.0f);
        put_u16(static_cast<uint16_t>(v));
    }
    return buf;
}

}  // namespace kokoro_common
