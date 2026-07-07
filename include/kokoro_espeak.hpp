// kokoro_espeak.hpp
// Optional espeak-ng G2P backend. The bundled ByT5 phonemizer is autoregressive
// without a KV cache and costs ~50 ms per generated phoneme (a 1400-char text
// spends ~55 s in it); espeak-ng is rule-based and phonemizes the same text in
// ~90 ms, with stress marks the ByT5 never emits.
//
// The library is loaded at runtime with dlopen/LoadLibrary so the build gains
// no new dependency: when libespeak-ng (and its espeak-ng-data package) is not
// installed, Engine silently keeps the bundled ByT5 phonemizer.
#pragma once

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace kokoro_espeak {

// espeak-ng voice for a BCP-47 language tag, or "" when the bundled ByT5 is
// the better choice (ja/zh: espeak's coverage does not match the phoneme set
// the Kokoro voices were trained on).
inline std::string voice_for_language(const std::string& language) {
    std::string lang;
    for (char c : language) lang += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto starts = [&](const char* p) { return lang.rfind(p, 0) == 0; };
    if (starts("en-gb")) return "en-gb";
    if (starts("en")) return "en-us";
    if (starts("fr")) return "fr";
    if (starts("es")) return "es";
    if (starts("it")) return "it";
    if (starts("pt-br")) return "pt-br";
    if (starts("pt")) return "pt";
    if (starts("hi")) return "hi";
    if (starts("de")) return "de";
    return "";
}

class Backend {
public:
    static Backend& instance() {
        static Backend backend;
        return backend;
    }

    bool available() const { return text_to_phonemes_ != nullptr; }

    // Phonemize `text` (no punctuation expected; the caller splits on it) with
    // the given espeak voice. Returns "" on any failure so the caller can fall
    // back to the bundled phonemizer.
    std::string phonemize(const std::string& text, const std::string& voice) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available()) return "";
        if (voice != current_voice_) {
            if (set_voice_by_name_(voice.c_str()) != 0) return "";
            current_voice_ = voice;
        }
        // espeak processes one clause per call and advances the pointer.
        std::string raw;
        const void* ptr = text.c_str();
        while (ptr) {
            const char* part = text_to_phonemes_(&ptr, kCharsUtf8, kPhonemesIpa);
            if (!part) break;
            if (!raw.empty()) raw += ' ';
            raw += part;
        }
        return clean(raw);
    }

private:
    // Matching subset of the stable espeak-ng C API (speak_lib.h).
    using InitializeFn = int (*)(int output, int buflength, const char* path, int options);
    using SetVoiceByNameFn = int (*)(const char* name);
    using TextToPhonemesFn = const char* (*)(const void** textptr, int textmode, int phonememode);

    static constexpr int kAudioOutputRetrieval = 1;  // no audio is produced
    static constexpr int kCharsUtf8 = 1;
    static constexpr int kPhonemesIpa = 0x02;

    Backend() {
#if defined(_WIN32)
        for (const char* name : {"libespeak-ng.dll", "espeak-ng.dll"})
            if ((handle_ = LoadLibraryA(name))) break;
        if (!handle_) return;
        const auto resolve = [&](const char* sym) {
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), sym));
        };
#else
        for (const char* name : {"libespeak-ng.so.1", "libespeak-ng.so",
                                 "libespeak-ng.1.dylib", "libespeak-ng.dylib"})
            if ((handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!handle_) return;
        const auto resolve = [&](const char* sym) { return dlsym(handle_, sym); };
#endif
        const auto initialize = reinterpret_cast<InitializeFn>(resolve("espeak_Initialize"));
        set_voice_by_name_ = reinterpret_cast<SetVoiceByNameFn>(resolve("espeak_SetVoiceByName"));
        const auto text_to_phonemes = reinterpret_cast<TextToPhonemesFn>(resolve("espeak_TextToPhonemes"));
        if (!initialize || !set_voice_by_name_ || !text_to_phonemes) return;
        // nullptr path = use the system espeak-ng-data; < 0 means init failed
        // (typically the data package is missing).
        if (initialize(kAudioOutputRetrieval, 0, nullptr, 0) < 0) return;
        text_to_phonemes_ = text_to_phonemes;
    }

    // Clean espeak IPA for the synthesis tokenizer: drop "(en)…(fr)"
    // language-switch markers, clitic hyphens ("la-"), tie bars, and newlines
    // (espeak emits one line per clause).
    static std::string clean(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size();) {
            const char c = in[i];
            if (c == '(') {
                const size_t j = in.find(')', i);
                if (j != std::string::npos && j > i + 1 && j - i <= 8) {
                    bool marker = true;
                    for (size_t k = i + 1; k < j && marker; ++k)
                        marker = std::isalpha(static_cast<unsigned char>(in[k])) || in[k] == '-';
                    if (marker) {
                        i = j + 1;
                        continue;
                    }
                }
            }
            if (c == '-') { ++i; continue; }
            if (in.compare(i, 2, "\xCD\xA1") == 0 ||  // U+0361 tie bar
                in.compare(i, 2, "\xCD\x9C") == 0) {  // U+035C tie bar below
                i += 2;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                ++i;
                continue;
            }
            out.push_back(c);
            ++i;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    void* handle_ = nullptr;
    SetVoiceByNameFn set_voice_by_name_ = nullptr;
    TextToPhonemesFn text_to_phonemes_ = nullptr;
    std::string current_voice_;
    std::mutex mutex_;
};

}  // namespace kokoro_espeak
