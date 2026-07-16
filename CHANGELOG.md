# Changelog

All notable changes to Replic are documented in this file.

## [0.9.0-rc.1] - 2026-07-15

### Added

- Blueprint-first replicated property reads and writes for supported scalar, struct, object, class, array, set, and map values.
- Typed Replic setter/getter nodes for arrays and enums.
- Container delta operations for arrays, sets, and maps.
- Replicated custom events with typed serialized, object-reference, and class-reference arguments.
- `None`, `OwnerOnly`, `ServerOnly`, and server-authoritative `Custom` permission modes.
- Persistent property and scene-component transform state for late joiners.
- Relative/world component transform replication with selectable location, rotation, and scale channels.
- Property observers with explicit `Is Bound` and `Unbind` lifecycle helpers.
- Runtime diagnostics with filterable write, event, state, observer, and permission log categories.
- Optional verbose and on-screen runtime diagnostics.
- `Has Replic Transport Component` and `Get Marked Property Debug Info` Blueprint diagnostic nodes.
- Public QuickStart, permissions, robustness, debugging, troubleshooting, and example-project planning documentation.
- Runtime, editor, PIE network, permission, travel, disconnect, scale, container, reference, observer, and diagnostic automation coverage.

### Changed

- Custom permission validation now uses authoritative server state instead of requiring private validation state on clients.
- Replic Custom Event nodes display both event mode and permission.
- Event dropdowns remain usable while a Blueprint contains a stale event selection.
- Variable/event Details changes support dirty state and Undo/Redo.
- Component transform settings are edited directly from the scene component Details panel.
- Routine runtime diagnostics are quiet by default and require explicit verbose logging.

### Known Scope

- Unreal Engine 5.6 and Win64 are the validated release-candidate environment.
- Replic is a source-code plugin and packaged games require a working C++ build setup.
- The raw `Set Marked Array` function remains available for compatibility, but `Replic Set Array` is the recommended workflow.
- A separate example project and Details-only automatic replication remain future work.

## [0.2.1] - 2026-06-06

- Initial public GitHub release.
