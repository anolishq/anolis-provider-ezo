#pragma once

#include <string>

namespace anolis_provider_ezo::logging {

void info(const std::string &message);
void warning(const std::string &message);
void error(const std::string &message);

}  // namespace anolis_provider_ezo::logging
