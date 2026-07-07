// server.cpp — kokoro-server
// OpenAI-compatible TTS HTTP server, shaped to slot into the same orchestration
// as llama-server / parakeet-server:
//   GET  /health               -> {"status":"ok"}   (readiness probe)
//   GET  /v1/voices            -> {"voices":[...]}   (discovery)
//   POST /v1/audio/speech      -> audio/wav          (OpenAI /v1/audio/speech)
//
// Launch contract mirrors parakeet-server:
//   kokoro-server --model <resource_dir> --host 127.0.0.1 --port <port>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

#include <cxxopts.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "kokoro_common.hpp"

using json = nlohmann::json;

namespace {

// Build an OpenAI-style error body so clients (and zallama's proxy) get a
// consistent shape on failure.
std::string error_body(const std::string& message, const std::string& type) {
    json j = {{"error", {{"message", message}, {"type", type}}}};
    return j.dump();
}

}  // namespace

int main(int argc, char* argv[]) {
    cxxopts::Options parser("kokoro-server", "Kokoro TTS — OpenAI-compatible HTTP server.");
    parser.add_options()
        ("m,model", "Resource directory containing the kokoro model files.",
            cxxopts::value<std::string>()->default_value("resources/"))
        ("host", "Host/interface to bind.", cxxopts::value<std::string>()->default_value("127.0.0.1"))
        ("p,port", "Port to listen on.", cxxopts::value<int>()->default_value("8080"))
        ("no-espeak", "Use the bundled (slow) ByT5 phonemizer even when libespeak-ng is installed.")
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

    const std::string model = args["model"].as<std::string>();
    const std::string host = args["host"].as<std::string>();
    const int port = args["port"].as<int>();

    // Load the model once at startup so the first request isn't slow and a bad
    // resource dir fails fast (before we claim to be healthy).
    std::cerr << "kokoro-server: loading model from '" << model << "'...\n";
    std::optional<kokoro_common::Engine> engine;
    try {
        engine.emplace(model, !args.count("no-espeak"));
    } catch (const std::exception& e) {
        std::cerr << "fatal: failed to load model: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "kokoro-server: phonemizer: "
              << (engine->espeak_g2p() ? "espeak-ng" : "ByT5 (bundled)") << "\n";
    // The engine is not thread-safe; serialize synthesis across httplib's worker
    // threads. TTS requests are CPU-bound and run one-at-a-time anyway.
    std::mutex engine_mutex;

    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"status", "ok"}}.dump(), "application/json");
    });

    server.Get("/v1/voices", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"voices", kokoro_common::voices()}}.dump(),
                        "application/json");
    });

    // OpenAI /v1/audio/speech. Body: {input, voice, speed?, language?,
    // response_format?}. We always return WAV; response_format is accepted but
    // only "wav"/"pcm" are honoured (others fall back to WAV with a warning
    // header) since the model emits PCM.
    server.Post("/v1/audio/speech", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content(error_body("Request body is not valid JSON.", "invalid_request_error"),
                            "application/json");
            return;
        }

        const std::string text = body.value("input", std::string{});
        const std::string voice = body.value("voice", std::string{"af_heart"});
        if (text.empty()) {
            res.status = 400;
            res.set_content(error_body("'input' is required.", "invalid_request_error"),
                            "application/json");
            return;
        }
        if (!kokoro_common::is_known_voice(voice)) {
            res.status = 400;
            res.set_content(error_body("Unknown voice '" + voice + "'.", "invalid_request_error"),
                            "application/json");
            return;
        }

        std::optional<float> speed;
        if (body.contains("speed") && body["speed"].is_number())
            speed = body["speed"].get<float>();
        std::optional<std::string> language;
        if (body.contains("language") && body["language"].is_string())
            language = body["language"].get<std::string>();

        const std::string fmt = body.value("response_format", std::string{"wav"});
        if (fmt != "wav" && fmt != "pcm")
            res.set_header("X-Kokoro-Warning", "response_format coerced to wav");

        try {
            std::vector<uint8_t> wav;
            {
                std::lock_guard<std::mutex> lock(engine_mutex);
                auto samples = engine->synthesize(text, voice, language, speed);
                wav = kokoro_common::encode_wav_pcm16(samples);
            }
            res.set_content(reinterpret_cast<const char*>(wav.data()), wav.size(),
                            "audio/wav");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(error_body(std::string("Synthesis failed: ") + e.what(),
                                       "server_error"),
                            "application/json");
        }
    });

    std::cerr << "kokoro-server: listening on http://" << host << ":" << port << "\n";
    if (!server.listen(host, port)) {
        std::cerr << "fatal: failed to bind " << host << ":" << port << "\n";
        return 1;
    }
    return 0;
}
