# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2026-03-09

Initial public release.

### Highlights

- Export selected Unreal Blueprints to compact deterministic JSON.
- Include a semantic index for control-flow, calls, latent tasks, and references.
- Preserve package-based output paths to avoid flat filename collisions.
- Record truncated traversal in `m[6]` and `x`, including external skipped graph provenance.
- Support Blueprint-defined struct and enum export.
