# Replic Troubleshooting

This page lists common Replic failures, their likely cause, and a direct fix.

## Replic Transport Is Missing or Component Settings Are Disabled

### Symptoms

- A warning asks for a `ReplicTransportComponent`.
- Component transform settings are disabled.
- A Replic node warns that its context has no transport component.

### Fix

1. Open the Actor Blueprint that owns the Replic property, event, or component.
2. Select `Class Defaults` and enable Unreal's `Replicates` setting.
3. In Components, press `Add`.
4. Search for `Replic Transport` and add it.
5. Compile the Blueprint.
6. Reselect the Scene Component to refresh its Details panel.

The transport must belong to the relevant Actor. Adding it to an unrelated Actor does not satisfy the target's setup.

## Event Not Found or EventName Is Invalid

### Symptoms

- `Replic Call Event` reports that no Replic-enabled event exists.
- The selected event appears as `(Invalid)`.
- The error starts after renaming or deleting a Custom Event.

### Fix

1. Open the Blueprint that owns the Custom Event.
2. Select the Custom Event node.
3. In Details, enable `Enable Replic`.
4. Set `Mode` and `Permission`.
5. Compile the target Blueprint.
6. Return to `Replic Call Event`.
7. Open `Event Name` and select the current event name.
8. Reconnect typed argument pins if the event signature changed.
9. Compile the caller Blueprint.

Replic deliberately does not silently redirect a stale event name. After a rename, select the new event explicitly so the call cannot target the wrong gameplay action.

## Property Not Found After a Variable Rename

### Symptoms

- A getter or setter reports that the old name is not a Replic-marked property.
- The variable exists under a new name, but existing Replic nodes still show the old name.

### Fix

1. Compile the Blueprint containing the renamed variable.
2. On every affected Replic getter/setter, open `Property Name`.
3. Select the new variable name.
4. Verify the value pin still has the expected type.
5. Compile all affected Blueprints.

Variable and event names are stored selections on the nodes. Renaming the Blueprint member does not automatically rewrite every existing Replic node.

## Wrong Target Object

### Symptoms

- The property or event dropdown is empty.
- Replic says the target does not own the selected property/event.
- The call succeeds on another Actor but not on the intended one.

### Fix

1. Identify the Blueprint that actually declares the variable or Custom Event.
2. Connect an object reference of that Blueprint type to `Target Object`.
3. Compile so the dropdown can resolve its marked members.
4. Select the property/event again.

Examples:

- An inventory variable declared on `BP_InventoryPlayerState` needs that PlayerState reference as `Target Object`.
- `ToggleDoor` declared on `BP_Door` needs the Door reference as `Target Object`.
- `Self` is correct only when the current Blueprint itself owns the selected member.

## Wrong Context Object

### Symptoms

- Replic warns that the context cannot resolve a transport path.
- A client request never reaches the server.
- Permission behavior uses the wrong requester.

### Fix

1. Use the object that initiates the request as `Context Object`.
2. Ensure its Actor path has a `ReplicTransportComponent`.
3. For player input, prefer the locally controlled Character, Pawn, Controller, or another owned replicated Actor as context.
4. Keep the object that owns the property/event connected separately to `Target Object`.
5. Do not use a generic unrelated UObject as context.

For a Character interacting with a Door:

```text
Context Object = interacting Character
Target Object  = Door
```

## Sound Plays Only on the Server

### Cause

`Play Sound at Location` is local Unreal behavior. Calling it in a `ServerOnly` event only plays it on the server.

### Fix

1. Keep the authoritative gameplay event, such as `ToggleDoor`, in `ServerOnly` mode.
2. Create a second Custom Event, such as `PlayDoorSound`.
3. Enable `Enable Replic` on `PlayDoorSound`.
4. Set its `Mode` to `ReplicateAll`.
5. Call `Play Sound at Location` inside `PlayDoorSound`.
6. At the end of the server event, call `PlayDoorSound` through `Replic Call Event`.
7. Use an argument such as `IsOpening` when open and close sounds differ.

Do not make old sounds persistent. Late joiners should receive current state, not historical audio.

## Late Joiner Receives a Default Value

### Fix for variables

1. Select the variable.
2. Enable `Enable Replic`.
3. Enable `Persistent State`.
4. Write it through the matching Replic setter node, not a normal Blueprint `Set` node.
5. Ensure the owning Actor exists and is replicated to the late client.

### Fix for component transforms

1. Select the Scene Component.
2. Enable the required transform channels under `Replic`.
3. Enable `Persistent State`.
4. Confirm the Actor has `Replicates` and `ReplicTransportComponent`.
5. Change the transform on the server.

Persistent state stores the latest accepted state. It does not replay old Custom Events.

## Observer Does Not Update UI or Text

### Fix

1. Call the refresh function once on `BeginPlay` for the initial display.
2. Call `Bind Marked Property Changed` for the correct `Target Object` and `Property Name`.
3. Store its returned observer object in a variable.
4. Bind one event to the observer's `On Changed` dispatcher.
5. Call the refresh function from that event.
6. Bind only once per property/object pair.
7. Unbind or release long-lived observers when their owning UI or gameplay object is destroyed.

If one change prints many times, the observer was probably bound repeatedly.

## Set Marked Array Reports an IntProperty Type Mismatch

### Cause

The raw generic `Set Marked Array` function can retain an incorrect wildcard/property type in Blueprint graphs.

### Fix

1. Remove the broken raw `Set Marked Array` node.
2. Add the typed node named `Replic Set Array`.
3. Connect the correctly typed target object.
4. Select the array in `Property Name`.
5. Connect the array value only after the property is selected.

`Replic Set Array` is the recommended and more stable array workflow. The raw function remains available for compatibility and advanced use.

## Client Interaction Feels Delayed

Authoritative changes require a client-to-server request and a replicated result back to clients. Some delay is expected on a real network.

Check these items before treating it as a plugin defect:

1. Disable artificial PIE packet lag and loss.
2. Make sure observers are bound once, not on a looping timer.
3. Avoid rebuilding an entire UI repeatedly for a local-only selection change.
4. Keep cosmetic local feedback separate from authoritative gameplay state where appropriate.
5. Test a packaged build between two machines to distinguish PIE multi-window overhead from network behavior.

Never make authoritative inventory or interaction state client-only just to hide latency.

## Custom Request Returns True but Nothing Changes

### Cause

For `Custom` permission, a client-side Return Value of `true` means the request was queued for authoritative validation. The server can still reject it.

### Fix

1. Open `Project Settings > Plugins > Replic > Debug`.
2. Enable `Enable Runtime Debug Logs`.
3. Enable `Enable Detailed Permission Logs`.
4. Reproduce the request and inspect the `Replic permission denied` message.
5. Confirm the target contains the expected `CanReplicWrite_<Property>` or `CanReplicCall_<Event>` function.
6. Confirm the function returns Boolean.
7. Use no input, or exactly one Actor-compatible requester input.
8. Confirm the server-side rule state returns true for the real requesting Actor.

Do not mirror private server validation flags to the client merely to make the request node return true. Custom validation is intentionally decided by the server.

## OwnerOnly Request Is Rejected

1. Confirm `Context Object` resolves to the locally owned Character, Pawn, PlayerController, or another owned replicated Actor.
2. Confirm the target is actually owned by the same Unreal network owner.
3. For shared world Actors such as doors, use a `Custom` server validation rule instead of assigning artificial ownership only to bypass the check.
4. Enable detailed permission logs and inspect `Requester`, `Target`, and `Stage`.

## Plugin Does Not Compile or Load

1. Confirm the project uses Unreal Engine 5.6.
2. Confirm `Replic.uplugin` is directly under `YourProject/Plugins/Replic`.
3. Install Visual Studio 2022 with `Desktop development with C++`, the MSVC toolchain, and a Windows SDK.
4. Close Unreal Editor and disable Live Coding before a full rebuild.
5. Generate Visual Studio project files again.
6. Build the project's `Development Editor` target.
7. If a Blueprint-only project has no build target, add one empty C++ class and restart Unreal.
8. Do not copy another project's `Intermediate` or `Binaries` folders as source.

## Packaged Game Has No Replic Behavior

1. Confirm the plugin is enabled in the project.
2. Package a C++ project target that includes the Replic runtime module.
3. Ensure only the runtime `Replic` module is needed in the packaged game; `ReplicEditor` is editor-only.
4. Test with actual server/client travel, not two standalone offline instances.
5. Inspect the packaged logs for `Replic`, transport, network, or permission warnings.

Replic runtime behavior is available in packaged Development and Shipping multiplayer builds when the plugin is built and included correctly.
