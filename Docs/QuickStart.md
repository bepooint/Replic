# Replic QuickStart

This guide shows small Blueprint patterns for Replic.

## Counter

1. Enable `Replicates` on the actor.
2. Add `ReplicTransportComponent`.
3. Create an integer variable, for example `Counter`.
4. Enable Replic for the variable in Details.
5. Create a Replic custom event, for example `IncrementCounter`.
6. In the event:
   - `Get Marked Int`
   - add `1`
   - `Set Marked Int`
7. Call it from another Blueprint with `Replic Call Event`.

Late joiners receive the latest persistent value when persistent state is enabled for the variable.

## Door Transform

1. Enable `Replicates` on the door actor.
2. Add `ReplicTransportComponent`.
3. Select the door mesh component.
4. In the component `Replic` category, enable `Replicate Rotation`.
5. Use `Relative Transform` for a normal door mesh.
6. Enable persistent transform state if late joiners should see the current door rotation.
7. Create a Replic custom event `ToggleDoor`.
8. Set the event mode to `ServerOnly`.
9. In `ToggleDoor`, change the door mesh rotation on the server.

The component transform replication sends the visible rotation to clients and late joiners.

## One-Shot Sounds

Unreal does not automatically replicate sound playback. Trigger sounds through Replic events.

For a door with separate open and close sounds:

1. Keep `ToggleDoor` as `ServerOnly`.
2. Create a second Replic custom event `PlayDoorSound`.
3. Set `PlayDoorSound` mode to `ReplicateAll`.
4. Add a bool input such as `IsOpening`.
5. In `PlayDoorSound`, branch on `IsOpening`.
6. Play the open sound for `true` and close sound for `false`.
7. At the end of `ToggleDoor`, call `PlayDoorSound` with `Replic Call Event`.

Do not make old sounds persistent. Late joiners should receive the door state, not replay old one-shot audio.
