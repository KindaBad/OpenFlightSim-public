# Typhoon maneuver mode and Luftwaffe model

## Maneuver mode

Press **M** in the Typhoon, or select **Maneuver mode [M]** in the flight
controls panel. The HUD shows MANEUVER MODE or MANEUVER STANDBY. It uses the
`Controls::maneuver_mode` flag the Su-57 already replicates, so protocol 13
is unchanged and existing servers accept it.

This is an OpenFlightSim gameplay control law, not the Eurofighter flight
control system. It shares the Su-57 schedule: full authority below 210 m/s,
fading linearly to none at 300 m/s, and inhibited with the gear down. At full
authority the canard law commands 2.2 times the normal pitch rate and 1.25
times the normal roll rate, shortens the response time by 30%, and moves the
soft angle-of-attack limit from 28 to 50 degrees.

The Typhoon has no vectoring nozzles, so the mode can only ask more of the
foreplanes and flaperons. Positive and negative G protection, surface travel
and rate limits, flow separation and energy loss all still apply. Bots never
select it.

`typhoon.maneuver` compares both modes from identical trims with identical
stick inputs over 1.5 s:

| TAS | Peak pitch rate normal / maneuver | Peak roll rate normal / maneuver |
|---|---|---|
| 140 m/s | 0.413 / 0.956 rad/s | 0.580 / 0.841 rad/s |
| 180 m/s | 0.388 / 0.923 rad/s | 0.527 / 0.777 rad/s |
| 210 m/s | 0.369 / 0.890 rad/s | 0.493 / 0.732 rad/s |

A full-aft pull from 140 m/s peaks at 33.1 degrees angle of attack against
30.1 degrees in normal mode, at 4.5 G, and recovers after switching the mode
off. The gain in reachable incidence is small: aerodynamic pitch authority,
not the protection, is what limits it. The test also checks that responses are
identical to normal law at 320 m/s and with the gear down, and that foreplane
and elevon positions stay within travel.

## Model

The Typhoon now uses bohmerang's Luftwaffe model under CC BY-NC-SA 4.0. See
[the asset record](../assets/aircraft/typhoon/README.md) for the source, the
import steps and the measured differences from published dimensions, and
[the release provenance](ASSET_RELEASE_PROVENANCE.md#typhoon-donor) for the
distribution review. The airbrake (H) is now animated.

Cockpit eye, exhaust, wingtip, gun and hit-sphere anchors in
`core/src/aircraft_definition.cpp` were re-measured from the new rig. The
flight model is unchanged apart from maneuver mode.

Checks run on Linux: the full CTest suite including `typhoon.assets`,
`assets.production` and `visual.hierarchy` against the new GLBs, and native
client captures of the parked, control-surface, gear-up, afterburner and
flight-deck views, kept under ignored `output/typhoon-luftwaffe/`. Physical
gamepad input and Windows rendering of this model were not checked locally.
