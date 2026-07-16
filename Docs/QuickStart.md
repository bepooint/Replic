# Replic QuickStart

This guide builds two small Blueprint examples with the current node-based Replic workflow:

- a persistent multiplayer counter
- a replicated door with late-join state and one-shot sounds

The examples use exact node and Details-panel names from Replic for Unreal Engine 5.6.

## 1. Before You Start

1. Install and enable Replic as described in the [README](../README.md#installation).
2. Open `Edit > Plugins` and verify that `Replic` is enabled.
3. Use a multiplayer GameMode and a playable map.
4. For PIE testing, open the Play menu and set:
   - `Number of Players` to `2`
   - `Net Mode` to `Play As Listen Server`
5. Do not enable Unreal `RepNotify` on Replic-managed variables. Replic observers provide change notifications.

Every Actor that owns Replic variables, Replic events, or Replic component-transform settings needs both prerequisites:

1. Select `Class Defaults` and enable Unreal's `Replicates` setting.
2. In the Components panel, press `Add` and add `Replic Transport` (`ReplicTransportComponent`).

## 2. Persistent Counter

### 2.1 Create the Actor

1. Create an Actor Blueprint named `BP_ReplicCounter`.
2. Open it and select `Class Defaults`.
3. Search for `Replicates` and enable it.
4. In Components, press `Add`.
5. Search for `Replic Transport` and add it.
6. Add a `Text Render` component named `CounterText`.
7. For a simple keyboard-only test, set `Auto Receive Input` to `Player 0` in Class Defaults. This is only a test shortcut; a real game should call the counter from its interaction or input system.

### 2.2 Create the Replic Variable

1. Create an Integer variable named `Counter`.
2. Select `Counter` in My Blueprint.
3. In the variable Details panel, open the `Replic` category.
4. Enable `Enable Replic`.
5. Enable `Persistent State`.
6. Leave `Use Batching` disabled for this example.
7. Set `Permission` to `None` for the first functional test.
8. Compile the Blueprint.

`Persistent State` stores the latest accepted value so a late joiner receives the current counter instead of the default value.

### 2.3 Create the Server Event

1. In the Event Graph, add a Custom Event named `IncrementCounter`.
2. Select the Custom Event node.
3. In its `Replic` category, enable `Enable Replic`.
4. Set `Mode` to `ServerOnly`.
5. Set `Permission` to `None` for this first example.
6. Compile.
7. From `IncrementCounter`, add `Get Marked Int`.
8. Set `Target Object` on `Get Marked Int` to `Self`.
9. Set `Property Name` to `Counter`.
10. Add an Integer `+` node and add `1` to the returned value.
11. Add `Set Marked Int`.
12. Connect the white execution pin from `IncrementCounter` to `Set Marked Int`.
13. Connect `Self` to `Context Object` on `Set Marked Int`.
14. Connect `Self` to `Target Object` on `Set Marked Int`.
15. Set `Property Name` to `Counter`.
16. Connect the result of the Integer `+` node to `Value`.

The event request reaches the server first. The server then writes the authoritative persistent value.

### 2.4 Call the Event

1. Add the keyboard event `E` to the Event Graph.
2. Add `Replic Call Event`.
3. Connect `E > Pressed` to the execution input of `Replic Call Event`.
4. Connect `Self` to `Context Object`.
5. Connect `Self` to `Target Object`.
6. Set `Event Name` to `IncrementCounter`.
7. Compile.

`Context Object` identifies the caller/request path. `Target Object` is the object that owns `IncrementCounter`. They are both `Self` in this self-contained test Actor, but they are not interchangeable in general.

### 2.5 Display Initial and Changed Values

1. Create a function named `RefreshCounterText`.
2. In the function, add `Get Marked Int`.
3. Set `Target Object` to `Self` and `Property Name` to `Counter`.
4. Convert the returned Integer to Text.
5. Connect the result to `Set Text` for `CounterText`.
6. Return to the Event Graph.
7. Create a variable named `CounterObserver` with type `Replic Property Observer Object Reference`.
8. From `Event BeginPlay`, call `RefreshCounterText` first. This displays the initial value before any change notification occurs.
9. After `RefreshCounterText`, add `Bind Marked Property Changed`.
10. Set `Target Object` to `Self` and `Property Name` to `Counter`.
11. Store its `Return Value` in `CounterObserver`.
12. Drag from `CounterObserver` and add `Bind Event to On Changed`.
13. Create a matching Custom Event named `OnCounterChanged` from the red Event pin.
14. Connect `OnCounterChanged` to `RefreshCounterText`.
15. Bind the observer only once. Repeated binding causes the same refresh to fire multiple times.
16. Before replacing `CounterObserver`, call `Is Bound` on the existing observer and call `Unbind` when it returns true.
17. For temporary listeners such as widgets, call `Unbind` when the listener is removed. Replic also unbinds automatically if the observed actor is destroyed.

### 2.6 Test the Counter

1. Place one `BP_ReplicCounter` in the map.
2. Start PIE with two players as a Listen Server.
3. Focus the host window and press `E` once.
4. Verify that both windows show `1`.
5. Focus the client window and press `E` once.
6. Verify that both windows show `2`.
7. For a real late-join test, keep the server running, start a second client afterward, and connect it to the server.
8. Verify that the late client immediately sees the latest counter value.

## 3. Persistent Door Transform

### 3.1 Create the Door Actor

1. Create an Actor Blueprint named `BP_ReplicDoor`.
2. Enable `Replicates` in Class Defaults.
3. Add `Replic Transport`.
4. Add a Static Mesh component named `DoorMesh`.
5. Position the mesh so its pivot produces the desired door movement.
6. For a simple test, set `Auto Receive Input` to `Player 0` and use a key such as `R`. In a real project, call the door through your interaction system.

### 3.2 Configure Transform Replication

1. Select `DoorMesh` in Components.
2. Open its `Replic` category in Details.
3. Set `Transform Space` to `Relative Transform`.
4. Enable `Replicate Rotation`.
5. Leave `Replicate Location` disabled.
6. Leave `Replicate Scale` disabled.
7. Enable `Persistent State`.
8. Leave `Advanced Settings` at their defaults for the first test.

If these settings are disabled and a prerequisite warning is visible, verify that the owning Actor has both `Replicates` enabled and a `ReplicTransportComponent`.

### 3.3 Create the Door State

1. Create a Boolean variable named `IsOpen`.
2. In its `Replic` category, enable `Enable Replic`.
3. Enable `Persistent State`.
4. Set `Permission` to `ServerOnly` because the server event will write the value.
5. Compile.

### 3.4 Create ToggleDoor

1. Add a Custom Event named `ToggleDoor`.
2. Enable `Enable Replic` in its `Replic` category.
3. Set `Mode` to `ServerOnly`.
4. Set `Permission` to `None` for the basic interaction test.
5. From the event, add `Get Marked Bool` for `IsOpen` with `Target Object = Self`.
6. Connect its value to `NOT Boolean`.
7. Add `Set Marked Bool`.
8. Connect `Self` to both `Context Object` and `Target Object`.
9. Set `Property Name` to `IsOpen`.
10. Connect the result of `NOT Boolean` to `Value`.
11. Continue the execution line into a `Branch`.
12. Connect the new Boolean value to `Condition`.
13. On `True`, call `Set Relative Rotation` on `DoorMesh` with the open rotation, for example Yaw `90`.
14. On `False`, call `Set Relative Rotation` on `DoorMesh` with the closed rotation, for example Yaw `0`.
15. Add the keyboard event `R`.
16. From `R > Pressed`, add `Replic Call Event`.
17. Set `Context Object = Self`, `Target Object = Self`, and `Event Name = ToggleDoor`.

Only the server changes the gameplay state and mesh rotation. Component transform replication sends the visible result to clients. Persistent transform state supplies the current rotation to late joiners.

## 4. Replicated Door Sounds

Unreal does not automatically replicate a call to `Play Sound at Location`. Replicate the one-shot trigger, not old audio playback.

1. Create a Custom Event named `PlayDoorSound`.
2. Add a Boolean input named `IsOpening`.
3. Enable `Enable Replic`.
4. Set `Mode` to `ReplicateAll`.
5. Set `Permission` to `ServerOnly` because `ToggleDoor` calls it on the server.
6. From `PlayDoorSound`, add a `Branch` using `IsOpening`.
7. On `True`, call `Play Sound at Location` with the opening sound.
8. On `False`, call `Play Sound at Location` with the closing sound.
9. Use `Get World Location` from `DoorMesh` for both sound locations.
10. After each `Set Relative Rotation` path in `ToggleDoor`, add `Replic Call Event`.
11. Set `Context Object = Self`, `Target Object = Self`, and `Event Name = PlayDoorSound`.
12. Connect `true` to `IsOpening` on the open path and `false` on the close path.

Expected behavior:

- host interaction: host and clients hear the sound
- client interaction: host and clients hear the sound
- late join: the door has the correct current rotation, but no old sound is replayed

## 5. State Versus One-Shot Events

Use persistent state for facts that must still be true when a client joins later:

- current counter value
- whether a door is open
- current component transform
- inventory contents

Use `ReplicateAll` one-shot events for actions that should happen once for currently connected peers:

- sounds
- particles
- camera feedback
- temporary cosmetic effects

Do not use persistent state to replay old one-shot effects. A late joiner needs the resulting door rotation, not every sound that was played before joining.

## 6. Next Steps

- Read [Permission Modes](Permissions.md) before using client-writable gameplay state.
- Read [Runtime Robustness](RuntimeRobustness.md) for tested scale baselines, travel behavior, reference arguments, and observer cleanup.
- Read [Debugging Replic](Debugging.md) for runtime log categories and diagnostic helper nodes.
- Use [Troubleshooting](Troubleshooting.md) for direct fixes to common setup and compile errors.
