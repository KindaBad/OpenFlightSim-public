// Shape of a jet engine's reheat plume, shared by both flame shaders.
//
// Distances are in the units of the nozzle model: the renderer scales them by
// each aircraft's exhaust length and radius.

#ifndef OFS_FLAME_GLSL
#define OFS_FLAME_GLSL

// Length of the plume at reheat 0..1.
float flameLength(float reheat) { return 0.8 + 5.6 * reheat; }

// How far each shell reaches relative to that length: the shock diamonds stop
// short of the flame, and the faint outer sheath runs on past it.
float flameReach(float layer) { return layer < 0.5 ? 0.80 : (layer < 1.5 ? 1.0 : 1.16); }

// Shock cells stand a fixed distance apart however long the plume is. A cell
// widens steadily to its Mach disc and narrows again, which gives the chain its
// diamond outline: 1 at the disc and 0 between cells, `along` metres from the
// nozzle.
float flameCell(float along) { return abs(fract((along - 0.36) / 0.72) * 2.0 - 1.0); }

// Reheat is a light source, and the eye adapts to it: at dusk it is brighter
// against the scene than at noon, but it keeps its colour rather than burning
// out to white. `exposure` is the frame's; daylight exposes at about 0.36.
float flameAdaptation(float exposure) { return pow(0.36 / max(exposure, 0.36), 0.78); }

#endif // OFS_FLAME_GLSL
