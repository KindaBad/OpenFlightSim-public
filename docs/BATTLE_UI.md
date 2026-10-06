# Battle interface

The flight interface uses a War Thunder inspired visual direction: compact,
shadowed white telemetry in the upper left, green flight symbology, blue remote
labels, charcoal square panels and muted red action buttons. No game assets or
branding are imported.

IAS is displayed in km/h, altitude in metres and vertical speed in m/s. Throttle,
Mach, gun ammunition and optional G load remain visible beside the aircraft.
Gear, flaps, vertical speed and flight/health status share a compact lower strip
at widths above 640 pixels. Stall warnings appear above the central sight.
The existing F1 settings, F4 HUD toggle and Tab camera controls are preserved.

Validation: Debug native client build and whitespace/diff checks passed.
A 180-frame Su-57 airborne run produced a reviewed 1280x800 capture and exited
cleanly. Flight readouts, heading, operations buttons and lower status strip
rendered correctly. Multiplayer target markers were not exercised in this run.
Generated captures remain local under the repository's source-only policy.
