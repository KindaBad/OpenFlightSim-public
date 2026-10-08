# Terrain

The airfield stands on a level plain in a broad valley. Low, rolling country
runs along the valley, farmed where it is flat and wooded where it is not, with
lakes in its hollows. Ranges of mountains rise on either side and beyond, with
crests, spurs and side valleys, rock on their steep faces and snow above about
1,800 m. The highest summits within 20 km of the field are near 2,700 m; a few
further out pass 4,000 m. The land is invented: it is not a real place.

## One shape for everything

`ofs::terrainElevation` (`core/include/ofs/terrain.hpp`) gives the height of
the ground anywhere, and everything else is derived from it. It is built from
smooth noise on an integer lattice, using only integer hashing, arithmetic and
square roots, so the server, every client and every bot compute the same
ground. Three things are added together:

- **Low country.** Five octaves of rolling ground a few tens of metres high,
  hollows that hold the lakes, and foothills where a range begins.
- **Where the ranges stand.** A slow field twenty-odd kilometres across, kept
  low near the field and along the valley that winds through it.
- **The mountains.** Nine octaves in which each counts for less where the
  ground is already steep, which leaves broad valley floors and sharp crests,
  multiplied by a ridged field so that no summit is a plateau. The ranges are
  bent out of line with the lattice they are made of.

Everything within 3.5 km of the middle of the runway is level, and the relief
grows in over the next 5 km.

## What an aircraft touches

Collision, the server and prediction use a triangle mesh sampled from that
function: 448 rings of 768 segments out to 64 km, the rings closer together
near the field. A triangle is about 60 m across at 5 km, 160 m at 20 km and
280 m at 60 km. `sampleTerrain` finds the triangle under a point and returns
its height and face normal. Beyond 64 km the function is used directly. The
client draws the same triangles, and continues them to the horizon.

Because the ground changed shape, and a client predicts its own aircraft over
it, everyone in a game needs the same version: this arrived with network
protocol 17.

## What is seen

The mesh is too coarse to light well by itself, so the client bakes the
function into a height map (`Landscape`, `client/src/landscape.cpp`): 96 km
square, 4,096 texels a side at the highest terrain setting (23 m each) and
2,048 otherwise, 16 bits a texel, with a chain of averaged levels. The surface
shader (`client/shaders/terrain_fs.glsl`) takes each pixel's slope from that
map at the scale the pixel covers, and marches it toward the sun for the long
shadows the relief casts. Outside the mapped square the slope comes from the
mesh. Rock shows wherever the slope is steep, and snow does not lie on it.

The ground's colour comes from material layers at two scales, all synthesised
at start-up (`client/src/procedural.cpp`). Cover layers are the country as it
looks from the air, 240 to 380 m to a tile: pasture with its drifts, worn
patches and stock paths, a drilled crop with its wheelings, stubble in swaths,
ploughland, and a woodland canopy of whole crowns. Detail layers are the
ground underfoot, a few metres to a tile, and are laid over the cover within a
kilometre of the eye. Open ground reads the pasture at two sizes, one taking
over from the other in drifts, so neither repeat shows.

Farmland is drawn as irregular fields a few hundred metres across, their
boundaries wandering. Most are pasture and green crops, some are cut for hay
or ripe, a few are ploughed, and each has its cover laid along its own lay and
tinted its own shade. About one field in a dozen is a wood. Seven boundaries
in ten carry a hedge or, for a quarter of all boundaries, a belt of trees
eleven metres to either side; the rest are a strip of rough grass. A hedge,
a belt and the edge of a wood are all outlined by the painted crowns that make
them up, with real trees standing in them. Grey lanes with grass verges wind
between blocks of fields. A field is wholly farmed or wholly meadow, so the
farmed land ends at a field's edge. The shader draws the pattern and
`Landscape::fieldPattern` computes the same one, which is how the trees find
the hedges, belts and woods.

From a height the painted canopy passes for trees. Lower down it is softened,
and within a few hundred metres it gives way to the wood's floor, on which the
real trees stand. Thorn and gorse grow in clumps on open ground that is not
farmed.

Land cover is classified from the same map: farmland on flat, low, dry ground;
woodland in groves that thin out at the treeline and on cliffs; lakes where a
closed hollow in the low country would hold water, filled to about half its
depth. Trees stand on the collision mesh, and nothing grows inside the
airfield's fence or under its approaches.

Where the low country is farmed there are villages, some forty of them: a
street of eight to twenty-seven houses with pitched roofs, and in the larger
ones a church with a spire. They are scenery, like the trees, and an aircraft
does not collide with them.

## Bots

A bot looks at the ground ahead of it as well as below: if its flight path
comes within 300 m of the ground in the next seven seconds it levels its wings
and climbs.

## Checking it

`environment.terrain` checks that contact heights match the drawn triangles to
within a tenth of a micrometre, that normals are perpendicular to their faces,
that the airfield is level and that the seam in the mesh is continuous.
`visual.landscape` checks the land cover, the lakes and the trees against the
collision surface. `regression.shader_sources` checks that the shader reads the
height map at the stage and scale the renderer supplies.

Viewpoints for looking at it:

```sh
./build/release/client/ofs_client --aircraft typhoon --visual-scenario valley --frames 60 --screenshot /tmp/valley.ppm
```

`valley`, `lake`, `ranges`, `mountains`, `village` and `environment` each start
paused at a different place.
