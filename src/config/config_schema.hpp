#pragma once

/**
 * @file config_schema.hpp
 * @brief The provider's declare-once config schema (SDK config toolkit).
 *
 * The single source of truth for the config contract: `--config-schema` emits
 * it (executable profile v1 §2) and `load_config` validates against it, so the
 * advertised schema and the enforced validation cannot drift.
 */

#include "anolis/provider_sdk/config.hpp"

namespace anolis_provider_ezo {

/** @brief The provider's config schema (built once, cached). */
const anolis::provider_sdk::config::Schema &provider_schema();

}  // namespace anolis_provider_ezo
