# Replic Permission Modes

Permissions control who may request a Replic property write or custom event call. They do not define where an accepted event executes.

For Custom Events, configure both fields:

- `Permission`: who is allowed to request the event
- `Mode`: where Replic dispatches the accepted event

## Permission Reference

| Permission | Accepted caller | Typical use |
| --- | --- | --- |
| `None` | Requests are accepted without an additional permission check | Prototypes, public switches, or logic already validated elsewhere |
| `OwnerOnly` | Only the owning client may request the change | A player's own inventory, selected slot, or character action |
| `ServerOnly` | Only server-initiated calls are accepted | Authoritative state written only by server gameplay logic |
| `Custom` | A validation function on the target decides | Distance checks, team rules, item requirements, cooldowns, or combined rules |

`None` does not mean local-only and it does not make untrusted client data safe. It only means Replic performs no extra permission rejection after resolving the request.

Every client request is still routed through the server. `OwnerOnly`, `ServerOnly`, and `Custom` are checked again on the authoritative server even if a normal Blueprint node can reject an obviously invalid request earlier on the client.

## Event Mode Reference

| Mode | Execution after server acceptance |
| --- | --- |
| `LocalOnly` | Executes only on the local resolved target |
| `ServerOnly` | Executes only on the server target |
| `OwnerOnly` | Executes on the server and is sent to the owning client |
| `ReplicateAll` | Executes on the server and is broadcast to all relevant clients |

### Common Combinations

| Use case | Permission | Mode |
| --- | --- | --- |
| Client asks server to open a public door | `None` or `Custom` | `ServerOnly` |
| Owning player changes personal state | `OwnerOnly` | `ServerOnly` |
| Server changes authoritative state | `ServerOnly` | `ServerOnly` |
| Server broadcasts a sound or VFX | `ServerOnly` | `ReplicateAll` |
| Validated interaction with distance/team rules | `Custom` | `ServerOnly` |

## Context Object and Target Object

These pins serve different purposes:

- `Context Object` identifies the caller/request path used by Replic.
- `Target Object` owns the Replic-marked property or Custom Event.

For a self-contained replicated Actor, both may be `Self`. For an interaction performed by a Character on a Door:

- `Context Object` is normally the interacting Character or another object with a valid `ReplicTransportComponent` path.
- `Target Object` is the Door that owns `ToggleDoor`.

Do not connect the Door to both pins merely because the requested event belongs to the Door. Permission checks that depend on the requester need the real caller context.

## Custom Validation

Select `Custom` when authorization depends on game rules. Replic looks for a validation function on the target object.

Custom validation runs against the authoritative target state on the server. The client does not need a replicated copy of private validation flags, cooldown state, inventory requirements, or server-only rule data.

For a variable named `InventorySlots`, use one of these function names:

```text
CanReplicWrite_InventorySlots()
CanReplicWrite_InventorySlots(RequestingActor)
```

For a Custom Event named `ToggleDoor`, use one of these function names:

```text
CanReplicCall_ToggleDoor()
CanReplicCall_ToggleDoor(RequestingActor)
```

The function must return a Boolean. Return `true` to accept the request and `false` to reject it.

The optional input must be an Actor type compatible with the actual requesting Actor. Custom validators cannot use additional inputs or output parameters.

For a client-side Replic setter or event caller using `Custom`, a `true` Return Value means the request was resolved and queued for the server. It does not mean the server accepted the custom rule. Observe the resulting replicated state or use a separate response event when gameplay needs an explicit acceptance result.

The Details panel provides `Create Custom Validation Function`. Use that button to create or open the correctly named function instead of typing its name manually.

Typical checks inside a custom validation function:

1. Verify `RequestingActor` is valid.
2. Verify the requester is close enough to the target.
3. Verify team, role, ownership, inventory, or cooldown rules.
4. Return `true` only after every required condition passes.

## Recommended Authority Pattern

For gameplay-relevant state:

1. Client input calls a Replic Custom Event.
2. The event uses `ServerOnly` mode.
3. Permission is `OwnerOnly` or `Custom` where appropriate.
4. The server validates the request.
5. The server writes the Replic-marked property.
6. Clients observe the resulting state.
7. Optional one-shot sound or VFX uses a separate `ReplicateAll` event.

This keeps the server authoritative while still giving every client the visible result.

## Server-Owned State Pattern

Use this for match state, shared doors, world objectives, scores, or any state clients must never write directly.

1. Set the state variable Permission to `ServerOnly`.
2. Keep the variable persistent when late joiners need its latest value.
3. Let authoritative server gameplay call the matching Replic setter.
4. Let clients read or observe the replicated result.
5. Do not call the state setter directly from client input.

If a client must request a change, send a separate server request event first. The server decides whether to update the `ServerOnly` state.

## Client Request To Server Event Pattern

Use this for interactions, abilities, inventory requests, buttons, or other client input that requires server approval.

1. Create a Replic Custom Event such as `RequestUseDoor`.
2. Set Event Mode to `ServerOnly`.
3. Set Permission to `OwnerOnly` when the target is meaningfully owned by the requesting player.
4. Otherwise set Permission to `Custom` and validate distance, team, inventory, cooldown, or other game rules on the server.
5. Use the locally owned Character, Pawn, or PlayerController as `Context Object`.
6. Use the Actor that declares the event as `Target Object`.
7. Inside the accepted server event, modify authoritative state with Replic setters.
8. Replicate optional one-shot sound or VFX through a separate `ReplicateAll` event.

## Permission Diagnostics

Open `Project Settings > Plugins > Replic > Debug` and enable:

1. `Enable Runtime Debug Logs`
2. `Enable Detailed Permission Logs`

Permission rejections include:

- permission mode
- validation stage (`ClientPreflight` or `ServerAuthority`)
- property or event name
- target path
- requester path
- exact reason, including missing or invalid Custom validator signatures

Keep screen debug messages disabled for production unless they are intentionally needed. Output logs are more suitable for diagnosing authority behavior.

## Ownership Caveat

`OwnerOnly` requires meaningful Unreal network ownership. A world-placed door is commonly server-owned and may not be owned by the interacting client. In that case, use a server request from the player's owned Character/Controller as the context, or use `Custom` validation for the door interaction.

If an `OwnerOnly` request is unexpectedly rejected, inspect the Actor's network owner before weakening the permission mode.
