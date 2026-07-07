// cli.cpp — kokoro-cli
// One-shot text-to-speech: synthesize a single string to a WAV file. Mirrors the
// parakeet-cli / llama-cli style (a small argv front-end over the engine) so it
// drops into the same tooling.
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include <cxxopts.hpp>

#include "kokoro_common.hpp"

int main(int argc, char* argv[]) {
    cxxopts::Options parser("kokoro-cli", "Kokoro TTS — synthesize text to a WAV file.");
    parser.add_options()
        ("m,model", "Resource directory containing the kokoro model files.",
            cxxopts::value<std::string>()->default_value("resources/"))
        ("t,text", "Text to synthesize.", cxxopts::value<std::string>())
        ("v,voice", "Voice id (see --list-voices).",
            cxxopts::value<std::string>()->default_value("af_heart"))
        ("l,language", "Language override.", cxxopts::value<std::string>())
        ("s,speed", "Speaking speed multiplier.", cxxopts::value<float>())
        ("o,output", "Output WAV path.", cxxopts::value<std::string>()->default_value("output.wav"))
        ("no-espeak", "Use the bundled (slow) ByT5 phonemizer even when libespeak-ng is installed.")
        ("list-voices", "Print the available voices and exit.")
        ("h,help", "Print help and exit.");

    cxxopts::ParseResult args;
    try {
        args = parser.parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    if (args.count("help")) {
        std::cout << parser.help() << "\n";
        return 0;
    }
    if (args.count("list-voices")) {
        for (const auto& v : kokoro_common::voices()) std::cout << v << "\n";
        return 0;
    }
    if (!args.count("text")) {
        std::cerr << "error: --text is required\n\n" << parser.help() << "\n";
        return 2;
    }

    const std::string model = args["model"].as<std::string>();
    const std::string text = args["text"].as<std::string>();
    const std::string voice = args["voice"].as<std::string>();
    const std::string output = args["output"].as<std::string>();
    auto language = args["language"].as_optional<std::string>();
    auto speed = args["speed"].as_optional<float>();

    if (!kokoro_common::is_known_voice(voice)) {
        std::cerr << "error: unknown voice '" << voice
                  << "'. Run with --list-voices to see the options.\n";
        return 2;
    }

    try {
        kokoro_common::Engine engine(model, !args.count("no-espeak"));
        std::cerr << "phonemizer: " << (engine.espeak_g2p() ? "espeak-ng" : "ByT5 (bundled)")
                  << "\n";
        auto samples = engine.synthesize(text, voice, language, speed);
        auto wav = kokoro_common::encode_wav_pcm16(samples);

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            std::cerr << "error: cannot open output file '" << output << "'\n";
            return 1;
        }
        out.write(reinterpret_cast<const char*>(wav.data()),
                  static_cast<std::streamsize>(wav.size()));
        out.close();

        const double seconds =
            static_cast<double>(samples.size()) / kokoro_common::kSampleRate;
        std::cerr << "wrote " << output << " (" << seconds << "s, "
                  << wav.size() << " bytes)\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
