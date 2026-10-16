Zstandard 1.5.7 decoder, from https://github.com/facebook/zstd/releases/tag/v1.5.7
(zstd-1.5.7.tar.gz, SHA-256 eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3).

`zstddeclib.c` is the single-file decoder generated, unmodified, by the release's
`build/single_file_libs/combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c`.
`zstd.h` and `zstd_errors.h` are the release's `lib/zstd.h` and `lib/zstd_errors.h`. BSD licence; see LICENSE.
OpenFlightSim uses it to read the Earth terrain pack (docs/WHOLE_EARTH_PLAN.md).
