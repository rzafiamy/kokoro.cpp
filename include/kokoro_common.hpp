// kokoro_common.hpp
// Shared helpers for kokoro-cli and kokoro-server: a thin engine wrapper around
// kokoro::kokoro_tts plus minimal WAV (PCM16) encoding. Kept header-only and
// dependency-light so both executables stay statically linkable.
#pragma once

#include <algorithm>
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

    // Synthesize `text` with `voice`, returning interleaved-mono float samples
    // in [-1, 1] at kSampleRate. `language`/`speed` follow the model defaults
    // when unset.
    std::vector<float> synthesize(
        const std::string& text,
        const std::string& voice,
        std::optional<std::string> language = std::nullopt,
        std::optional<float> speed = std::nullopt) {
        muna::Tensor out = model_(text, voice, language, speed);
        const auto num_frames = out.shape()[0];
        const auto num_channels = out.dims() > 1 ? out.shape()[1] : 1;
        std::vector<float> samples(static_cast<size_t>(num_frames) * num_channels);
        const float* data = out.data<float>();
        std::copy(data, data + samples.size(), samples.begin());
        return samples;
    }

private:
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
