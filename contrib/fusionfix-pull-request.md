# Pull request for ThirteenAG/GTAIV.EFLC.FusionFix

PR: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/pull/1652
Branch: `GianFadiga:turn-indicators-auto-with-manual` (based on master 5a961b6; patch: `contrib/fusionfix-turn-indicators-auto-with-manual.patch`)

## Title
Turn indicators: Automatic + manual mode, bumper tap fix, GPS-driven blinkers

## Description
Three changes to the turn indicators, one commit each:

1. **Automatic and manual indicators together**, chosen in the menu.
2. **A bumper tap no longer fires the weapon or pulls the handbrake** with manual indicators.
3. **The automatic indicators follow the GPS route** (waypoint or mission destination).

### 1. Turn Indicators menu option (`MISC.TurnIndicators`)
Until now, turning manual indicators on (`TURNINDICATORS.ManualTurnIndicators`, editor only, restart) switched the
automatic steering-based blinkers off, so players had to pick one or the other. The option is now a choice:

| Value | Name | Behaviour |
|---|---|---|
| 0 | Off | no turn indicators (unchanged) |
| 1 | Automatic | the indicators come on by themselves when steering (unchanged) |
| 2 | On (manual) | the player switches them: tap LB/RB or the indicator keys; tap both for hazards (what `ManualTurnIndicators = 1` did) |
| 3 | Automatic + manual | both: automatic indicators keep working, and an indicator or hazard switched on by hand takes priority until the player switches it off |

- Existing setups keep their meaning: 0 and 1 are unchanged, and `ManualTurnIndicators = 1` still turns Automatic
  into On (manual). Its description now points to the new option.
- The mode is read every frame, so changing it in the menu applies straight away (no restart).
- Texts for all 7 languages.

### 2. Bumper tap only works the indicator
With manual indicators, LB/RB also do what the game binds them to in a vehicle (fire/aim, handbrake), so tapping a
bumper for the indicator also fired the weapon or pulled the handbrake.

While the player drives with manual indicators (On (manual) or Automatic + manual), the game only gets LB/RB once the
button has been held longer than a tap: 300 ms, the same threshold the tap detection already used. This is done in an
`XInputGetState` hook; the indicator logic reads the real button state.

- Tap: only the indicator.
- Hold: fires/aims or pulls the handbrake as before, about 300 ms later.
- On foot, or with manual indicators off, nothing changes.

### 3. GPS-driven automatic indicators (`TURNINDICATORS.GpsTurnIndicators`, default on)
With a GPS route and the automatic indicators on (Automatic or Automatic + manual), the indicators also signal the
route's next turn, the way the GPS voice announces it. Steering still takes priority.

- `GENERATE_DIRECTIONS`, the call behind the GPS voice, gives the next instruction towards a point. The codes were
  found by driving logged routes: 0 none, 1 turn right, 2 turn left, 3 straight on, 4 turn around. It reports a turn
  from shortly before the junction until the turn is made, and is polled every 250 ms.
- The route target is the waypoint blip (sprite 8) if one is set, otherwise the blip with the route flag (set by
  `SET_ROUTE`, flag `0x10` at `+0x20`), which covers mission destinations. Both are read from the radar's blip
  array, found by pattern (`8B 04 85 ? ? ? ? 80 78 08 00 75 0C A1`).
- FusionFix's `NativeInvoke` only copies `Vector3` out-parameters back for natives that return a value.
  `GET_BLIP_COORDS` and `GENERATE_DIRECTIONS` are void, so they are called through a context and the vectors are
  copied back explicitly; otherwise the coordinates read as 0,0,0.
- New setting `TURNINDICATORS.GpsTurnIndicators` (editor, default on) to turn it off, with texts in all languages.

### Options summary
| Setting | Where | Default | Effect |
|---|---|---|---|
| `MISC.TurnIndicators` | Game menu | Off | Off / Automatic / On (manual) / Automatic + manual |
| `TURNINDICATORS.GpsTurnIndicators` | editor | on | automatic indicators also follow the GPS route |
| `TURNINDICATORS.ManualTurnIndicators` | editor | off | legacy: makes Automatic behave as On (manual) |
| `TURNINDICATORS.LeftIndicatorKey` / `RightIndicatorKey` | editor | `[` / `]` | keyboard keys for the manual indicators |

### Testing
Tested in game on a backport of these changes to 5.1.2 (same code; menu row added through `frontend_menus.xml`),
GTA IV Complete Edition 1.2.0.59, keyboard and controller:

- all four modes, switched live from the menu;
- LB/RB tap (indicator only) and hold (fire/handbrake), hazards with both bumpers;
- GPS indicators on map-waypoint routes and on mission routes.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01J2EEBdczaXVKRDjePb6ox8

## Replies to the PR comments

**To ClaudeLib** ("Does tapping LB/RB still perform the fire weapon/pull handbrake action when manual control is enabled?"):

> Good catch: it did. The second commit fixes it: while driving with manual indicators, the game only gets LB/RB once
> the button is held longer than a tap (300 ms, the same threshold as the tap detection). Tapping only works the
> indicator; holding still fires/aims or pulls the handbrake, slightly delayed. On foot, or with manual indicators
> off, nothing changes.

**To Fifteensencher** ("automatic turn signals that would automatically activate before turning along the GPS route"):

> Added in the third commit. With a GPS route (a map waypoint or a mission destination) and automatic indicators on,
> the indicator comes on shortly before each turn the GPS voice announces and goes off once the turn is made.
> Steering still takes priority. It is on by default and can be turned off with
> `TURNINDICATORS.GpsTurnIndicators`.
