# Replic

Replic is a Blueprint-first replication plugin for Unreal Engine 5.6.

It provides explicit Blueprint nodes for replicated property writes, reads, events, observers, permissions, persistent late-join state, and component transform replication.

Current version: `0.9.0`

This is the validated `0.9.0` release on the path to the stable `1.0.0` feature set. The explicit node-based workflow remains the supported default until the v1.0 criteria are complete.

## Preview

![Replic multiplayer counter demo](Docs/Images/multiplayer-counter-demo.png)

<table>
  <tr>
    <td align="center" width="50%">
      <img src="Docs/Images/replic-door-sound-event.png" width="100%" alt="Replicated door sound event"><br>
      <sub>Replicated door logic with server-side state and replicated sound trigger</sub>
    </td>
    <td align="center" width="50%">
      <img src="Docs/Images/component-transform-settings.png" width="100%" alt="Component transform replication settings"><br>
      <sub>Component transform replication with persistent late-join state</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="Docs/Images/replic-custom-event-settings.png" width="100%" alt="Replic custom event settings"><br>
      <sub>Custom event replication settings directly in Blueprint details</sub>
    </td>
    <td align="center" width="50%">
      <img src="Docs/Images/typed-event-arguments.png" width="100%" alt="Typed event argument nodes"><br>
      <sub>Typed named values for replicated event arguments</sub>
    </td>
  </tr>
</table>

## Features

- Runtime transport component for replicated actors
- Replic-marked Blueprint variables
- Explicit setter and getter nodes
- Replicated custom events
- Typed event arguments
- Replicable sound triggers through Replic events
- Array, set, and map support
- Container delta operations
- Property change observers
- Persistent state for late joiners
- Permission modes:
  - `None`
  - `OwnerOnly`
  - `ServerOnly`
  - `Custom`
- Scene component transform replication:
  - relative or world transform
  - location, rotation, and scale channels
  - optional persistent transform state

## Requirements

- Unreal Engine 5.6
- Visual Studio 2022 with C++ build tools

## Installation

### Install from a GitHub release

1. Close the Unreal Editor.
2. Download the ZIP attached to the desired Replic release.
3. Create a `Plugins` folder next to your project's `.uproject` file if it does not exist.
4. Extract the plugin so the descriptor is located here:

```text
YourProject/Plugins/Replic
YourProject/Plugins/Replic/Replic.uplugin
```

5. Make sure there is no extra nested folder such as `Replic/Replic-main/Replic.uplugin`.
6. Open the project. Allow Unreal to rebuild missing modules when prompted.
7. Open `Edit > Plugins`, search for `Replic`, enable it, and restart the editor if requested.

### Install from source

1. Clone or download this repository.
2. Copy the repository contents into `YourProject/Plugins/Replic`.
3. Right-click the `.uproject` and choose `Generate Visual Studio project files` if Unreal cannot build the plugin automatically.
4. Build the `Development Editor` target in Visual Studio 2022, or open the project and accept Unreal's rebuild prompt.

Replic contains C++ runtime and editor modules. Blueprint-only projects may need one empty C++ class once so Unreal creates the project's C++ build target. A prebuilt plugin package must match the Unreal Engine version and target platform.

### Verify the installation

1. Create or open an Actor Blueprint.
2. Press `Add` in the Components panel.
3. Search for `Replic Transport`.
4. If `ReplicTransportComponent` is available, the plugin is loaded.

If installation or compilation fails, use the direct fixes in [Troubleshooting](Docs/Troubleshooting.md).

## Basic Usage

1. Enable `Replicates` on the actor that owns the replicated state.
2. Add `ReplicTransportComponent` to that actor.
3. Create a Blueprint variable or custom event.
4. Enable the Replic settings in the Details panel.
5. Use Replic nodes such as:
   - `Set Marked Int`
   - `Get Marked Int`
   - `Replic Set Array`
     - Prefer `Replic Set Array` over the raw `Set Marked Array` function. The typed Replic node is the recommended and more stable array workflow.
   - `Add To Marked Array`
   - `Replic Call Event`
   - `Bind Marked Property Changed`

For one-shot sounds such as doors, pickups, buttons, or impacts, trigger `Play Sound at Location` from a Replic custom event. Use persistent state for the gameplay result, not for replaying old sounds to late joiners.

For actor state changes, keep gameplay authority on the server and replicate the visible result. A common pattern is:

- `ServerOnly` event for the authoritative state change
- `ReplicateAll` event for one-shot cosmetic effects such as sounds
- persistent property or component transform state for late joiners

## Documentation

- [QuickStart: counter, door, sound, and late join](Docs/QuickStart.md)
- [Permission modes and authority patterns](Docs/Permissions.md)
- [Runtime robustness, scale baselines, travel, and observer cleanup](Docs/RuntimeRobustness.md)
- [Debugging, log categories, and diagnostic helper nodes](Docs/Debugging.md)
- [Troubleshooting with direct fixes](Docs/Troubleshooting.md)
- [Changelog](CHANGELOG.md)

## Packaging Note

For packaged builds, use a C++ project or an equivalent C++ build setup so Unreal can build and link the plugin runtime module.

## License

MIT License. See `LICENSE`.

## Star History

<a href="https://www.star-history.com/?repos=bepooint%2FReplic&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=bepooint/Replic&type=date&theme=dark&legend=bottom-right" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=bepooint/Replic&type=date&legend=bottom-right" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=bepooint/Replic&type=date&legend=bottom-right" />
 </picture>
</a>
