# Runtime Robustness

This page describes the multiplayer scenarios covered by Replic's UE 5.6 automation tests and the runtime practices expected from a project using the plugin.

## Validated Baseline

The current private test suite covers:

- listen-server sessions with 2, 4, and 6 total players
- host disconnect, client disconnect, and client reconnect
- non-seamless and seamless map travel
- 32 replicated actors in one scenario
- 24 replicated scene components on one actor
- a burst of 64 replicated custom events
- arrays, sets, and maps with 512 entries each
- native object-reference and class-reference custom-event arguments
- persistent property and component-transform state for late joiners
- multiple independent observers on one property
- explicit observer unbinding and automatic cleanup when the observed actor is destroyed

These values are regression-test baselines, not hard limits or performance guarantees. Network budget, update frequency, payload size, relevance, and gameplay code still determine the practical scale of a project.

## Observer Lifetime

`Bind Marked Property Changed` returns a `Replic Property Observer` object.

1. Store the returned observer in a variable for as long as notifications are needed.
2. Bind each logical listener once. Rebinding on every UI refresh creates duplicate callbacks.
3. Before replacing a stored observer, call `Is Bound` on the old observer.
4. If it is bound, call `Unbind`.
5. Call `Unbind` when a temporary UI or gameplay listener is torn down.

Replic automatically unbinds an observer when its observed actor is destroyed. Explicit `Unbind` is still recommended for listeners whose lifetime ends before the actor is destroyed.

## Travel Behavior

Non-seamless travel destroys the old world and its actors. Recreate the actor in the destination map, then continue using Replic normally.

Seamless travel can preserve Replic actor state when Unreal carries that actor into the destination world. Use Unreal's normal seamless-travel rules, including `GetSeamlessTravelActorList`, for actors that must survive. Replic does not override Unreal's actor travel lifecycle.

Persistent state is intended for clients that join an existing world. It does not turn an actor destroyed by non-seamless travel into a cross-map save object.

## Containers And Event Bursts

Large container states are supported, but repeatedly sending complete large arrays, sets, or maps is more expensive than sending focused changes. Prefer Replic's container delta operations for frequent add, remove, or update workflows.

One-shot events are suitable for gameplay notifications, sounds, and effects. Avoid using a large event stream as a replacement for persistent state. Replicate the durable result as a marked property or component transform.

## Object And Class Arguments

Replic custom events preserve native object and class references when the receiving event pin expects the matching reference type. The referenced object must still follow Unreal's normal networking rules: actors and components need a valid network identity, and both peers must be able to resolve the reference.

Do not rely on transient local-only UObject instances being resolvable on another machine.

## Practical Validation

Before shipping a project, repeat the important scenarios with the project's actual actors and payloads:

1. Test the expected maximum player count.
2. Test late join while persistent state already exists.
3. Test disconnect and reconnect.
4. Test every map-travel path used by the game.
5. Profile large containers and high-frequency events under realistic latency and packet loss.
6. Verify temporary observers are unbound when their owning UI or system is removed.
