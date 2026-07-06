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

// --- English text normalization ---------------------------------------------
// The ByT5 G2P phonemizer only speaks plain words: digits, ALL-CAPS tokens and
// symbols like $/% come out garbled or silently dropped ("42" was spoken as
// "at", "NASA" as "eneza", "AI" disappeared). Upstream Kokoro normalizes text
// before phonemization (misaki); this port skipped that step, which is what
// made output sound cut/incorrect. This is a minimal English normalizer:
//   - integers/decimals ("42", "3.14", "1,000") -> number words
//   - "$42" / "$4.50" -> dollars and cents, "7%" -> "seven percent"
//   - ordinals "1st".."42nd" -> "first".."forty second"
//   - ALL-CAPS: pronounceable words are lowercased ("FINISH" -> "finish"),
//     acronyms are spelled out ("AI" -> "a i") — the phonemizer reads isolated
//     lowercase letters as letter names.
// Only applied when the language is English (the default); other languages
// pass through unchanged.

inline std::string int_to_words_en(unsigned long long n) {
    static const char* kOnes[] = {
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
        "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* kTens[] = {"",      "",      "twenty",  "thirty", "forty",
                                  "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 20) return kOnes[n];
    if (n < 100) {
        std::string s = kTens[n / 10];
        if (n % 10) s += std::string(" ") + kOnes[n % 10];
        return s;
    }
    if (n < 1000) {
        std::string s = std::string(kOnes[n / 100]) + " hundred";
        if (n % 100) s += " " + int_to_words_en(n % 100);
        return s;
    }
    static const std::pair<unsigned long long, const char*> kScales[] = {
        {1000000000000ULL, "trillion"},
        {1000000000ULL, "billion"},
        {1000000ULL, "million"},
        {1000ULL, "thousand"}};
    for (const auto& [value, name] : kScales)
        if (n >= value) {
            std::string s = int_to_words_en(n / value) + " " + name;
            if (n % value) s += " " + int_to_words_en(n % value);
            return s;
        }
    return "";
}

// Read a digit string one digit at a time ("007" -> "zero zero seven"). Used
// for decimal fractions and numbers too long to say as one quantity.
inline std::string digits_to_words_en(const std::string& digits) {
    std::string out;
    for (char d : digits) {
        if (!std::isdigit(static_cast<unsigned char>(d))) continue;
        if (!out.empty()) out += ' ';
        out += int_to_words_en(static_cast<unsigned long long>(d - '0'));
    }
    return out;
}

inline std::string number_to_words_en(const std::string& digits_with_commas) {
    std::string digits;
    for (char c : digits_with_commas)
        if (std::isdigit(static_cast<unsigned char>(c))) digits.push_back(c);
    if (digits.empty()) return "";
    if (digits.size() > 15) return digits_to_words_en(digits);
    return int_to_words_en(std::stoull(digits));
}

inline std::string ordinal_to_words_en(unsigned long long n) {
    std::string words = int_to_words_en(n);
    const size_t pos = words.find_last_of(' ');
    std::string last = pos == std::string::npos ? words : words.substr(pos + 1);
    if (last == "one") last = "first";
    else if (last == "two") last = "second";
    else if (last == "three") last = "third";
    else if (last == "five") last = "fifth";
    else if (last == "eight") last = "eighth";
    else if (last == "nine") last = "ninth";
    else if (last == "twelve") last = "twelfth";
    else if (last.back() == 'y') last = last.substr(0, last.size() - 1) + "ieth";
    else last += "th";
    return pos == std::string::npos ? last : words.substr(0, pos + 1) + last;
}

// Normalize one whitespace-delimited token's core (punctuation already split
// off by normalize_token_en). Returns the replacement, or `core` unchanged.
inline std::string normalize_core_en(const std::string& core) {
    if (core.empty()) return core;
    const bool currency = core.front() == '$';
    std::string body = currency ? core.substr(1) : core;
    const bool percent = !body.empty() && body.back() == '%';
    if (percent) body.pop_back();
    if (body.empty()) return core;

    const auto is_digits = [](const std::string& s, bool commas) {
        if (s.empty()) return false;
        bool any = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) any = true;
            else if (!(commas && c == ',')) return false;
        }
        return any;
    };

    // Ordinals: "1st", "22nd", "3rd", "45th" (also uppercase suffixes).
    if (!currency && !percent && body.size() > 2) {
        std::string suffix = body.substr(body.size() - 2);
        for (char& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string head = body.substr(0, body.size() - 2);
        if ((suffix == "st" || suffix == "nd" || suffix == "rd" || suffix == "th") &&
            is_digits(head, true) && head.size() <= 15) {
            std::string digits;
            for (char c : head)
                if (c != ',') digits.push_back(c);
            return ordinal_to_words_en(std::stoull(digits));
        }
    }

    // Plain integers and decimals, with optional $ prefix / % suffix.
    const size_t dot = body.find('.');
    const std::string int_part = dot == std::string::npos ? body : body.substr(0, dot);
    const std::string frac_part = dot == std::string::npos ? "" : body.substr(dot + 1);
    if (is_digits(int_part, true) && body.find('.', dot + 1) == std::string::npos &&
        (frac_part.empty() ? dot == std::string::npos : is_digits(frac_part, false))) {
        std::string words = number_to_words_en(int_part);
        if (currency && frac_part.size() == 2) {
            words += int_part == "1" ? " dollar" : " dollars";
            const std::string cents = number_to_words_en(frac_part);
            words += " and " + cents + (frac_part == "01" ? " cent" : " cents");
        } else {
            if (!frac_part.empty()) words += " point " + digits_to_words_en(frac_part);
            if (currency) words += (int_part == "1" && frac_part.empty()) ? " dollar" : " dollars";
        }
        if (percent) words += " percent";
        return words;
    }

    // ALL-CAPS words and acronyms (optionally with digits, e.g. "GPT4").
    if (!currency && !percent) {
        // Split off a possessive so "NASA's" still matches.
        std::string stem = body, tail;
        if (stem.size() > 2 && (stem.compare(stem.size() - 2, 2, "'s") == 0 ||
                                stem.compare(stem.size() - 2, 2, "'S") == 0)) {
            tail = "'s";
            stem = stem.substr(0, stem.size() - 2);
        }
        bool caps = stem.size() >= 2, has_letter = false, has_digit = false, has_vowel = false;
        for (char c : stem) {
            if (c >= 'A' && c <= 'Z') {
                has_letter = true;
                if (std::strchr("AEIOU", c)) has_vowel = true;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                has_digit = true;
            } else {
                caps = false;
                break;
            }
        }
        if (caps && has_letter) {
            if (!has_digit && has_vowel && stem.size() >= 4) {
                // Pronounceable word shouted in caps: just lowercase it.
                for (char& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return stem + tail;
            }
            // Acronym: spell letters, read digit runs as numbers.
            std::string out;
            for (size_t i = 0; i < stem.size(); ++i) {
                if (!out.empty()) out += ' ';
                if (std::isdigit(static_cast<unsigned char>(stem[i]))) {
                    size_t j = i;
                    while (j < stem.size() && std::isdigit(static_cast<unsigned char>(stem[j]))) ++j;
                    out += number_to_words_en(stem.substr(i, j - i));
                    i = j - 1;
                } else {
                    out += static_cast<char>(std::tolower(static_cast<unsigned char>(stem[i])));
                }
            }
            return out + tail;
        }
    }
    return core;
}

// Split leading/trailing ASCII punctuation off a token, normalize the core,
// and reattach the punctuation so prosody marks (.,!?) survive.
inline std::string normalize_token_en(const std::string& tok) {
    size_t b = 0, e = tok.size();
    const auto keep_lead = [](unsigned char c) {
        return std::isalnum(c) || c >= 0x80 || c == '$';
    };
    const auto keep_trail = [](unsigned char c) {
        return std::isalnum(c) || c >= 0x80 || c == '%';
    };
    while (b < e && !keep_lead(static_cast<unsigned char>(tok[b]))) ++b;
    while (e > b && !keep_trail(static_cast<unsigned char>(tok[e - 1]))) --e;
    if (b >= e) return tok;
    const std::string core = tok.substr(b, e - b);
    const std::string repl = normalize_core_en(core);
    if (repl == core) return tok;
    return tok.substr(0, b) + repl + tok.substr(e);
}

inline std::string normalize_text_en(const std::string& text) {
    std::string out, tok;
    out.reserve(text.size() + 16);
    const auto flush = [&] {
        if (tok.empty()) return;
        out += normalize_token_en(tok);
        tok.clear();
    };
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            out.push_back(c);
        } else {
            tok.push_back(c);
        }
    }
    flush();
    return out;
}

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
// cap it is hard-split on whitespace. The input is normalized (English only)
// and sanitized first. Returns trimmed, non-empty chunks; falls back to the
// whole (sanitized) text as one.
inline std::vector<std::string> split_into_chunks(
    const std::string& raw_text, size_t max_chars = kMaxChunkChars,
    bool english = true) {
    const std::string text =
        sanitize_text(english ? normalize_text_en(raw_text) : raw_text);
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
        const bool english = !language || language->rfind("en", 0) == 0;
        const auto chunks = split_into_chunks(text, kMaxChunkChars, english);
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
