# Su-57 maneuver mode and condensation

Press **M** in the Su-57, or select **Maneuver mode [M]** in the flight controls
panel. The HUD shows NORMAL, MANEUVER, or MANEUVER / STANDBY. Runway/airborne
reset restores normal mode. Mode selection is sent to the authoritative server
and retained through owner prediction, snapshot replay and remote presentation.
Client and server must both use protocol 13. The Typhoon shares the key and
flag with its own control law; see [TYPHOON_MANEUVER_MODE.md](TYPHOON_MANEUVER_MODE.md).

This is an OpenFlightSim engineering/gameplay control law, not a reconstruction
of the real Su-57 system. The existing feedback controller and independent
canted-nozzle allocator remain active. With gear retracted, maneuver authority
is full below 210 m/s and blends linearly to zero at 300 m/s. Full authority
raises the nominal pitch-rate command to 2.8 times normal and roll-rate command
to 1.35 times normal, shortens the response time by 35%, and gives the physical
thrust-vectoring nozzles more allocation priority even before flow separation.
This makes ordinary combat-speed stick movements noticeably sharper as well as
supporting high-incidence flight. It changes the soft AoA
protection from the configured 35 degrees to 80 degrees. Gear down inhibits the
extension. Positive/negative G protections, physical aerodynamic separation,
energy loss, engine output, surface bounds and finite nozzle slew remain active.
The aircraft still requires sufficient thrust and room to recover; the mode
never writes attitude or angular velocity directly.

Wing vapor is automatic and independent of mode selection. F1's Effects and
overlays panel has **Wing condensation** and **Relative humidity** controls.
Humidity defaults to 0.75 and is a local, persisted visual atmosphere parameter,
not a meteorological forecast or replicated weather field. Existing authoritative
steady wind and temperature offset feed the relative-airflow calculation.

A qualitative pressure-drop estimate combines dynamic pressure with excess
load or high incidence. Expansion cooling and a saturation-pressure estimate
then test whether local humidity exceeds saturation. Vapor requires moving,
airborne aircraft, airspeed over 65 m/s, temperature above 238 K, and Medium/High
quality. Cruising at low incidence/load and flying in dry air produces no vapor.
The Su-57 emits one thin translucent layer along the swept leading edges,
with a wing-plane normal that follows bank rather than facing the camera.
Short chord-scale lifetimes, limited patch overlap and opacity proportional to
condensation strength keep the aircraft visible. Soft procedural density
variation feathers the edges. Emissions sampled between frames retain their
actual age, avoiding a stack of fresh bright patches on slow frames. Narrow tip
vortex wisps follow relative airflow; negative loads place the wing bands below
the wings. Particles advect with the wind and evaporate quickly after unloading. This bounded
particle approximation is not CFD, an icing model or a Su-57 fidelity claim.

The qualitative humidity/cooling basis follows NASA's
[Observation of airplane flow fields by natural condensation effects](https://ntrs.nasa.gov/citations/19880034912).
The actual pressure-drop coefficients and patch locations are estimates.
Cold-altitude exhaust contrails remain separate: their broad envelope also
requires humidity above 0.5 and powered, healthy engines. That envelope is not a
Schmidt–Appleman contrail forecast.

## Verification

A clean source snapshot containing only this task's changes passed all **39**
selected CTests: core, all Su-57 scenarios/assets/replay/live multiplayer,
physical closure and energy cases, particle/material/animation checks,
protocol/prediction, and quantization/input/presentation replication checks.

The maneuver regression compares integrated normal/maneuver responses from
identical trims at 140, 180 and 210 m/s, with identical stick inputs. Peak pitch
motion is 2.4–2.6 times faster and roll motion is 36–38% faster over 1.5 seconds.
The entry/recovery case starts at 140 m/s and 3,000 m, reaches 43.479 degrees
AoA, then recovers to 5.140 degrees after switching to normal mode and commanding
nose-down. It checks actuator angle/slew bounds, finite state, high-speed and
gear-down inhibition. Packet tests cover both boolean values, invalid flags,
quantization/input change detection, remote presentation and live server receipt.
Condensation tests cover humid high-G and high-AoA emission plus dry, cruise,
stationary, slow, ground, disabled, low-quality and zero-relative-airflow gates,
and evaporation after unloading. Additional assertions check swept band
orientation on both wings, chord-scale lifetime, atmospheric advection,
airflow-aligned tip wisps, banked negative-load emission at Medium quality,
and invalid orientation rejection.

The native client compiled and linked in the current checkout using the pinned
local graphics dependencies. A deterministic condensation fixture was rendered
and inspected in chase and close orbit views; captures remain under ignored
`output/su57-maneuver-impact/`. Reproduce with:

```sh
./build/release/client/ofs_client --aircraft su57 \
  --visual-scenario condensation --orbit --orbit-distance 22 \
  --orbit-yaw 2.35 --orbit-pitch .35 --frames 180 \
  --screenshot output/su57-maneuver-impact/condensation.ppm
```

The captures verify condensation placement and appearance. Physical gamepad
testing and Windows graphics validation were not part of these checks.
