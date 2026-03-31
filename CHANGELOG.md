# Changelog

All notable changes to `anolis-provider-ezo` are documented in this file.

## [Unreleased]

### Changed

1. Simplified mixed-bus validation assets to two canonical baseline flows:
   - Windows mock validation.
   - Linux real-hardware validation.
2. Consolidated operator commands into `config/mixed-bus/COMMANDS.md`.
3. Updated docs to reference the minimal baseline workflow with descriptive names.
4. Flattened `docs/` into canonical top-level documents and removed nested directory structure.
5. Aligned Linux hardware preset naming with provider-bread by adding `dev-linux-hardware-*` aliases in provider-ezo and updating mixed-bus configs/runbooks to use them.

### Removed

1. Legacy conflict-focused validation assets from the active baseline validation path.
