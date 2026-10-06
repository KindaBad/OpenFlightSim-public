# Terrain, vegetation and afterburner refinement

Ground shading blends meadow colors, moisture, irregular soil patches and
filtered grass detail. Fine surface relief uses screen derivatives while the
shared terrain collision surface stays unchanged. Cropland appears sparsely on
flat land with warped boundaries rather than covering the whole valley with a
repeated rectangular grid.

Trees now have tapered trunks and branches, scalloped broadleaf crowns or
irregular layered pine boughs, varied lean and coloration, and grass tufts at
their roots. Continuous density noise produces groves and clearings across
scenery patches. Larger patches fill gaps while preserving runway, building and
service-road clearance. Near and distant meshes are batched, with bounded
geometry budgets; nearby shadows use the detailed foliage silhouette.

Afterburner UVs are clamped before fractional powers in both flame shaders.
Small interpolation overshoots at plume edges previously produced NaN radiance
that spread into black rectangles through HDR/bloom. The GPU regression draws
the shipped plume shaders with UVs just below zero and above one, checks that
the background is preserved, and checks that the plume interior still glows.

Verification: release client and shader compilation, six targeted
visual/combat/environment tests, the GPU shader conformance suite, and native
low-altitude ground, forest and rear-afterburner captures on Linux/OpenGL.
The source-only repository policy keeps generated captures local under
`output/terrain-refresh/`. Windows/D3D11 runtime output remains unverified.
