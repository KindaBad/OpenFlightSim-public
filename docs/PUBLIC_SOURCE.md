# Public source publication

The public source repository is
[KindaBad/OpenFlightSim-public](https://github.com/KindaBad/OpenFlightSim-public).
It starts from a clean snapshot of the current source, without importing the
private repository's Git history. The original OpenFlightSim repository remains
private so its historical development work and local-source history are preserved.

The clean baseline excludes research-only `reference/` documents/images,
generated aircraft GLBs and Blender files, imported restricted reference data,
build/dependency caches, audit archives and generated validation output.
Current source changes untrack the unused research files while preserving the
local copies. Original source scripts, shader code, tests, physical model
parameters, authored small textures and third-party notices remain available.

The old private history contains `output/Su57-Felon.blend` and four Su-57 GLBs
from commit `267d0e2`, despite their removal in `bddc88b`. The current reference
PDF also carries an explicit restriction on reproduction. Making the original
repository public would expose those earlier files. Deleting current files
does not remove historical blobs. The clean public repository avoids rewriting
or force-pushing the private history.

Publication review scanned 1,133 reachable text blobs up to 8 MiB on the private
`origin/main` history for targeted GitHub/Slack/Google/AWS credential patterns,
embedded credential URLs and private-key headers; no matches were found.
This is a targeted check, not a complete security certification. The local audit
report stays under `.cache/publication-review/`.

Windows Preparation automatically builds and tests public `main` pushes and pull
requests with standard GitHub-hosted Windows runners. Its job condition keeps
private-repository builds manual. The launcher Qt tests run with the offscreen
platform. A passing source build does not include the aircraft content required
for a playable game distribution.

The clean snapshot passes all 75 launcher tests, including Qt widgets, and its
fresh CMake configuration passes `launcher.security_config` through CTest.

Player releases use the separate release workflow and GitHub Releases as their HTTPS download host,
runtime/dependency notices and an approved aircraft pack. Public source publication
does not permit publishing the CGTrader Su-57 as loose model/source files. Its
user-supplied listing permits incorporated game use with asset safeguards; the
reviewed protected-content path is documented in [protected aircraft packaging](PROTECTED_AIRCRAFT.md).
The first player release (0.3.0) omitted the Su-57. Versions 0.3.1 to 0.4.2
shipped that donor as an encrypted game-content pack. From 0.4.3 the Su-57 is a
Creative Commons model shipped unencrypted; source artwork remains excluded. Record
the original author, source and applicable license when preparing the pack; see
[asset provenance](ASSET_RELEASE_PROVENANCE.md) and [release packaging](LAUNCHER.md).

To update the public source, copy reviewed changes into its independent checkout,
commit normally and push to its own `origin`. Never push the private repository's
branches, tags, old history, ignored files or local aircraft binaries into it.
The repository instructions require this source-only delivery after completed
source tasks, so subsequent updates trigger Windows builds in the public repo.
