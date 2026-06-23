#include "logging/logger.hpp"

#include <iostream>

namespace anolis_provider_ezo::logging {
namespace {

void write(const char *level, const std::string &message) {
    std::cerr << "[anolis-provider-ezo] [" << level << "] " << message << '\n';
}

}  // namespace

void info(const std::string &message) { write("INFO", message); }

void warning(const std::string &message) { write("WARN", message); }

void error(const std::string &message) { write("ERROR", message); }

}  // namespace anolis_provider_ezo::logging
