# Pull request for ThirteenAG/GTAIV.EFLC.FusionFix

Branch: `turn-indicators-auto-with-manual` (patch: `contrib/fusionfix-turn-indicators-auto-with-manual.patch`, based on master 5a961b6)

## Title
Turn indicators: Off / Automatic / On (manual) / Automatic + manual in the menu

## Description
Today, enabling manual turn indicators (`TURNINDICATORS.ManualTurnIndicators`, editor only, restart) switches the
automatic, steering-based blinkers off entirely, so players have to choose one or the other.

This makes `MISC.TurnIndicators` a choice instead of a bool:

| Value | Behaviour |
|---|---|
| 0 Off | unchanged |
| 1 Automatic | unchanged: the blinkers come on by themselves when steering |
| 2 On (manual) | the player switches them on: tap LB/RB or the turn indicator keys (what `ManualTurnIndicators = 1` did) |
| 3 Automatic + manual | both: automatic blinkers keep working, and a blinker or hazard switched on by hand takes priority until the player switches it off |

- Existing settings keep their meaning: 0 and 1 are unchanged, and `ManualTurnIndicators = 1` still turns
  Automatic into On (manual), so nobody's setup changes.
- The mode is read every frame, so it applies live from the menu (no restart).
- Texts added for all 7 languages; the `ManualTurnIndicators` description now points to the new option.

Implementation: `VehicleState` gets `manual_active`, set when the player switches a blinker on by hand and cleared
when it is switched off (or hazards are toggled); the automatic block runs when the mode allows it and no manual
blinker/hazard is active.

Tested in game on a 5.1.2 backport (same logic, menu row via frontend_menus.xml): GTA IV CE 1.2.0.59. <!-- TODO: confirm after the in-game test -->
