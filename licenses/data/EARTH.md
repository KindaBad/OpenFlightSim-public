# Earth map data

The Earth map's terrain pack (`earth.ofse`), land cover (`earth-cover.ofsc`)
and detail regions (`earth-detail-*.ofsd`) are built by
`scripts/build_earth_terrain.py` from these sources. They are downloaded
separately from the game.

| Data | Source | Licence |
|---|---|---|
| Heights of land and sea floor | NOAA National Centers for Environmental Information, ETOPO 2022 15 arc-second Global Relief Model, doi:10.25921/fd45-gt74 | Public domain (US Government work) |
| Coastlines, minor islands and lakes | Natural Earth, 1:10m physical vectors (naturalearthdata.com) | Public domain |
| Towns and cities | Natural Earth, 1:10m populated places (naturalearthdata.com) | Public domain |
| Land cover colour | Natural Earth II, by Tom Patterson (naturalearthdata.com) | Public domain |
| Airports and runways | OurAirports (ourairports.com) | Public domain |
| Detailed heights on land (detail regions) | Copernicus DEM GLO-90 (Copernicus WorldDEM-90), from the AWS Open Data copy by Sinergise | Licence for the use of the Copernicus WorldDEM-90: free use, reproduction, distribution and adaptation with the notices below |

The detail regions are produced using Copernicus WorldDEM™-90 © DLR e.V.
2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under
COPERNICUS by the European Union and ESA; all rights reserved. The data has
been resampled onto the game's grid, and its heights lowered under lakes and
the sea.

The organisations in charge of the Copernicus programme by law or by
delegation do not incur any liability for any use of the Copernicus
WorldDEM™-90. OpenFlightSim's use of it is not endorsed by Airbus Defence and
Space, the European Union, ESA or anyone else in charge of the Copernicus
programme. Copernicus is a trade mark of the European Union; WorldDEM™ is a
trade mark of Airbus Defence and Space GmbH.

The game reads the pack with the Zstandard decoder, © Meta Platforms, Inc.
and affiliates, under the BSD licence in `core/thirdparty/zstd/LICENSE`.
