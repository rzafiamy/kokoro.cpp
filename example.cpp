#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <cxxopts.hpp>
#include "AudioFile.h"
#include "kokoro.kokoro_tts.hpp"

int main(int argc, char *argv[]) {
    // Define CLI
    cxxopts::Options parser("kokoro_tts", "Run transpiled `kokoro_tts` C++ function.");
    parser.add_options()
        ("text", "Input `text` argument.", cxxopts::value<std::string>())
        ("voice", "Input `voice` argument.", cxxopts::value<std::string>())
        ("language", "Input `language` argument.", cxxopts::value<std::optional<std::string>>())
        ("speed", "Input `speed` argument.", cxxopts::value<std::optional<float>>())
        ("h,help", "Print help and exit.");
    auto args = parser.parse(argc, argv);
    if (args.count("help")) {
        std::cout << parser.help() << std::endl;
        return 0;
    }
    // Deserialize inputs
    std::string text = args["text"].as<std::string>();
    std::string voice = args["voice"].as<std::string>();
    std::optional<std::string> language = args["language"].as_optional<std::string>();
    std::optional<float> speed = args["speed"].as_optional<float>();
    // Invoke function
    muna::Configuration configuration("resources/");
    kokoro::kokoro_tts kokoro_tts(configuration);
    auto output0 = kokoro_tts(text, voice, language, speed);
    // Display outputs
    {
        auto output_path = std::filesystem::temp_directory_path() / "output0.wav";
        AudioFile<float> audio_file;
        auto num_frames = output0.shape()[0];
        auto num_channels = output0.dims() > 1 ? output0.shape()[1] : 1;
        audio_file.setNumChannels(static_cast<int>(num_channels));
        audio_file.setNumSamplesPerChannel(static_cast<int>(num_frames));
        audio_file.setSampleRate(24000);
        audio_file.setBitDepth(24);
        for (size_t c = 0; c < num_channels; ++c)
            for (size_t f = 0; f < num_frames; ++f)
                audio_file.samples[c][f] = output0.data<float>()[f * num_channels + c];
        audio_file.save(output_path.string());
        std::cout << "output0: " << output_path << std::endl;
        #if defined(__APPLE__)
            std::system(("open " + output_path.string()).c_str());
        #elif defined(_WIN32)
            std::system(("start " + output_path.string()).c_str());
        #elif defined(__linux__)
            std::system(("xdg-open " + output_path.string()).c_str());
        #endif
    }
    // Exit
    return 0;
}