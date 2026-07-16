# Debugging Replic

Replic provides opt-in runtime logs, filterable log categories, short on-screen messages, and Blueprint diagnostic helpers. Debug output is disabled by default so normal games and packaged builds are not noisy.

## Enable Runtime Diagnostics

1. Open `Edit > Project Settings`.
2. Open `Plugins > Replic`.
3. In `Debug`, enable `Enable Runtime Debug Logs`.
4. Keep `Enable Verbose Runtime Logs` disabled when you only need failures and permission rejections.
5. Enable `Enable Verbose Runtime Logs` when you need the complete request, server apply, dispatch, observer, batching, and persistent-state flow.
6. Expand the advanced Debug settings and enable only the channels relevant to the problem.

The available channels are:

- Write Debug Logs: property writes and container delta operations.
- Event Debug Logs: event requests, authoritative application, dispatch, and local execution.
- State Debug Logs: persistent-state commits, batching, replication, and late-join reapplication.
- Observer Debug Logs: observer binding, unbinding, broadcasts, and callbacks. This is disabled by default because observer-heavy UI can generate many messages.
- Detailed Permission Logs: OwnerOnly, ServerOnly, and Custom permission rejections with the validation stage and reason.

`Enable Screen Debug Messages` is a lightweight runtime overlay for short PIE diagnostics. It follows the same channel and verbose settings. Keep it disabled for normal play and production builds.

## Log Categories

Use the Output Log category filter to isolate one part of the Replic flow:

- `LogReplic`: general Replic runtime messages that do not belong to a narrower channel.
- `LogReplicWrites`: property and container writes.
- `LogReplicEvents`: replicated event calls and dispatch.
- `LogReplicState`: persistent state, batching, and late-join synchronization.
- `LogReplicObservers`: observer lifetime and notifications.
- `LogReplicPermissions`: rejected permission checks and Custom validation failures.

Warnings and errors remain visible when runtime logging is enabled even if verbose logging is disabled. Routine success messages require `Enable Verbose Runtime Logs`.

## Blueprint Diagnostic Nodes

### Has Replic Transport Component

`Has Replic Transport Component` checks whether Replic can resolve the supplied object and whether its host actor currently owns a `ReplicTransportComponent`.

The node is read-only. It never creates a component. Use it to distinguish a missing transport setup from a failed property or event configuration.

### Get Marked Property Debug Info

`Get Marked Property Debug Info` takes a target object and property name and returns `Replic Property Debug Info`.

The snapshot contains:

- target resolution and transport presence
- property existence and type
- whether Replic is enabled for the property
- current local serialized value
- configured permission, persistent-state, and batching settings
- whether a persistent value currently exists on the local transport
- the locally stored persistent value
- a short diagnostic message describing the first failed check

The node only inspects local state. On a client, its persistent value is the state currently received by that client. It does not query the server and it does not alter gameplay state.

## Reading Common Failures

### A write returns false immediately

1. Filter for `LogReplicWrites`.
2. Check that Context Object and Target Object can be resolved.
3. Run `Has Replic Transport Component` for the target.
4. Run `Get Marked Property Debug Info` and inspect `Property Found`, `Replic Enabled`, and `Diagnostic Message`.

### A client request is sent but the value does not change

1. Filter for `LogReplicPermissions` and `LogReplicWrites`.
2. A permission message reports `Mode`, `Stage`, `Subject`, `Target`, `Requester`, and `Reason`.
3. `Stage=ClientPreflight` means the request was rejected before sending where that check is safe.
4. `Stage=ServerAuthority` means the authoritative server rejected the request.
5. For `Custom`, fix the named validation function or the authoritative condition reported in `Reason`.

### An event is not executed

1. Filter for `LogReplicEvents` and `LogReplicPermissions`.
2. Confirm that the event exists, is Replic-enabled, and is selected on the caller node.
3. Check whether the request was accepted, which dispatch mode was used, and whether a local target executed the event.

### A late joiner receives the wrong state

1. Filter for `LogReplicState`.
2. Confirm that the property or component has persistent state enabled.
3. Look for the state commit on the server and the late-join reapplication on the joining client.
4. Use `Get Marked Property Debug Info` on both server and client to compare `Local Value`, `Has Persistent State`, and `Persistent Value`.

## Diagnostic UI Scope

v0.8 intentionally uses the standard Output Log, the optional lightweight on-screen messages, and Blueprint snapshot nodes instead of adding a separate editor debug window or a permanent runtime overlay. This keeps diagnostics available without adding another editor panel or shipping UI.
