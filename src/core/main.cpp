#include <exception>
#include <iostream>
#include <string>

#include "anolis/provider_sdk/config.hpp"
#include "anolis/provider_sdk/runtime.hpp"
#include "config/config_schema.hpp"
#include "config/provider_config.hpp"
#include "core/ezo_provider_runtime.hpp"
#include "core/runtime_state.hpp"
#include "logging/logger.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void set_binary_mode_stdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void print_usage(const char *program_name) {
    std::cout << "Usage:\n"
              << "  " << program_name << " --version\n"
              << "  " << program_name << " --config-schema\n"
              << "  " << program_name << " --check-config <path>\n"
              << "  " << program_name << " --config <path>\n\n"
              << "Implements ADPP v1 provider for EZO devices.\n";
}

}  // namespace

int main(int argc, char **argv) {
    anolis_provider_ezo::runtime::reset();

    std::string config_path;
    bool check_config_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << ANOLIS_PROVIDER_EZO_VERSION << '\n';
            return 0;
        }
        if (arg == "--config-schema") {
            // Configless (executable profile v1 §2): the envelope is the only
            // stdout output; diagnostics go to stderr. Handled before any
            // other work can touch stdout.
            anolis::provider_sdk::config::EnvelopeOptions options;
            options.provider_name = "anolis-provider-ezo";
            options.extra_string_entries.emplace_back("provider_version", ANOLIS_PROVIDER_EZO_VERSION);
            anolis::provider_sdk::config::write_config_schema_envelope(std::cout,
                                                                       anolis_provider_ezo::provider_schema(), options);
            return 0;
        }
        if (arg == "--check-config" && i + 1 < argc) {
            config_path = argv[++i];
            check_config_only = true;
            continue;
        }
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            continue;
        }
        print_usage(argv[0]);
        return 1;
    }

    if (config_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const anolis_provider_ezo::ProviderConfig config = anolis_provider_ezo::load_config(config_path);

        if (check_config_only) {
            anolis_provider_ezo::logging::info("Config valid: " + anolis_provider_ezo::summarize_config(config));
            return 0;
        }

        anolis_provider_ezo::logging::info("starting with config: " + anolis_provider_ezo::summarize_config(config));
        anolis_provider_ezo::runtime::initialize(config);
    } catch (const std::exception &e) {
        anolis_provider_ezo::logging::error(e.what());
        return 1;
    }

    const anolis_provider_ezo::runtime::RuntimeState startup_state = anolis_provider_ezo::runtime::snapshot();
    if (startup_state.ready) {
        anolis_provider_ezo::logging::info(startup_state.startup_message);
    } else {
        anolis_provider_ezo::logging::warning(startup_state.startup_message);
    }

    set_binary_mode_stdio();
    anolis_provider_ezo::logging::info("ready (transport=stdio+uint32_le)");

    // The SDK run-loop owns the transport, the §3.2 Hello-gate, dispatch, and exit
    // codes; ezo supplies the device/inventory/readiness seam. on_shutdown stops the
    // I2C BusExecutor on every loop exit (was RuntimeShutdownGuard).
    anolis_provider_ezo::EzoProviderRuntime ezo_runtime;
    anolis::provider_sdk::LifecycleHooks hooks;
    hooks.on_shutdown = [] { anolis_provider_ezo::runtime::shutdown(); };

    return anolis::provider_sdk::run_loop(std::cin, std::cout, ezo_runtime, hooks);
}
