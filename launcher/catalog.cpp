// Export the actual compiled registry. Neither the launcher nor release tooling
// maintains a second list of supported aircraft or claims unavailable modes.
#include "ofs/aircraft_definition.hpp"
#include "ofs/theatres.hpp"
#include "ofs/weapons.hpp"
#include "ofs_version.hpp"
#ifdef OFS_NETWORK_ENABLED
#include "ofs/net/protocol.hpp"
#endif
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) { std::cerr << "ofs_catalog OUTPUT.json [EARTH_DETAIL.json]\n"; return 2; }
  std::ofstream out(argv[1]);
  out << "{\"schema\":1,\"version\":" << std::quoted(OFS_VERSION)
      << ",\"modes\":[\"free\"";
#ifdef OFS_NETWORK_ENABLED
  out << ",\"multiplayer\",\"dogfight\"";
#endif
  out << "]";
#ifdef OFS_NETWORK_ENABLED
  // Lets the launcher tell which games on the local network this build can join.
  out << ",\"protocol\":" << ofs::net::protocolVersion;
#endif
  // The round Earth's theatres and the airports to start at in each. The
  // terrain pack itself is a separate download.
  out << ",\"theatres\":[";
  for (std::size_t t = 0; t < ofs::theatres().size(); ++t) {
    const auto& theatre = ofs::theatres()[t];
    out << (t ? "," : "") << "{\"id\":" << std::quoted(std::string(theatre.id)) << ",\"name\":" << std::quoted(std::string(theatre.name))
        << ",\"airports\":[";
    for (std::size_t a = 0; a < theatre.airports.size(); ++a)
      out << (a ? "," : "") << "{\"ident\":" << std::quoted(std::string(theatre.airports[a].ident))
          << ",\"name\":" << std::quoted(std::string(theatre.airports[a].name)) << "}";
    // The GLO-90 detail regions its games use.
    out << "],\"regions\":[";
    const auto regions = ofs::theatreRegions(theatre);
    for (std::size_t r = 0; r < regions.size(); ++r) out << (r ? "," : "") << std::quoted(regions[r]);
    out << "]}";
  }
  out << "]";
  // The Earth map's data as downloads, when this build was told where they are.
  if (std::string_view(OFS_EARTH_PACK_URL).size() && std::string_view(OFS_EARTH_COVER_URL).size()) {
    out << ",\"earth_data\":[";
    out << "{\"name\":\"earth.ofse\",\"url\":" << std::quoted(OFS_EARTH_PACK_URL) << ",\"sha256\":" << std::quoted(OFS_EARTH_PACK_SHA256)
        << ",\"size\":" << OFS_EARTH_PACK_SIZE << "},";
    out << "{\"name\":\"earth-cover.ofsc\",\"url\":" << std::quoted(OFS_EARTH_COVER_URL) << ",\"sha256\":"
        << std::quoted(OFS_EARTH_COVER_SHA256) << ",\"size\":" << OFS_EARTH_COVER_SIZE << "}]";
  }
  // And the detail regions, checked by the release workflow.
  // The checked list of detail regions, read from a file: a string literal that
  // long does not compile with MSVC.
  if (argc == 3 && argv[2][0]) {
    std::ifstream detail(argv[2]);
    if (!detail) { std::cerr << "cannot read " << argv[2] << "\n"; return 2; }
    std::stringstream text;
    text << detail.rdbuf();
    out << ",\"earth_detail\":" << text.str();
  }
  out << ",\"aircraft\":[";
  bool first = true;
  for (const auto& d : ofs::aircraftDefinitions()) {
    if (!first) out << ',';
    first = false;
    ofs::weapons::Inventory loadout;
    loadout.reset(d.type);
    out << "{\"id\":" << std::quoted(std::string(d.key))
        << ",\"name\":" << std::quoted(std::string(d.displayName))
        << ",\"model\":" << std::quoted(std::string(d.modelAsset))
        << ",\"armed\":" << (d.gun ? "true" : "false")
        // The bomber's two engine slots each stand for the four under one wing.
        << ",\"engines\":" << (d.bomber ? d.flight.engine_count * 4 : d.flight.engine_count)
        << ",\"span_m\":" << d.flight.wing_span
        << ",\"reference_mass_kg\":" << d.flight.mass
        << ",\"empty_mass_kg\":" << d.flight.empty_mass
        << ",\"fuel_capacity_kg\":" << d.flight.fuel_capacity
        << ",\"wing_area_m2\":" << d.flight.wing_area
        << ",\"thrust_dry_kn\":" << d.flight.engine_count * d.flight.thrust_sl_static_each / 1000
        << ",\"thrust_reheat_kn\":" << d.flight.engine_count * d.flight.afterburner_thrust_each / 1000
        << ",\"g_limit\":" << d.flight.g_positive
        << ",\"gun_rounds\":" << (d.gun ? d.gun->ammo : 0)
        << ",\"heat_seekers\":" << loadout.remaining(ofs::weapons::WeaponType::Infrared)
        << ",\"radar_missiles\":" << loadout.remaining(ofs::weapons::WeaponType::ActiveRadar)
        << ",\"bomber\":" << (d.bomber ? "true" : "false")
        << ",\"bomb_loads\":[";
    for (unsigned load = 0; load < ofs::weapons::bombLoadouts(d.type); ++load)
      out << (load ? "," : "") << std::quoted(std::string(ofs::weapons::bombLoad(d.type, load).name));
    out << "]"
        << ",\"lods\":[";
    bool firstLod = true;
    for (const auto& lod : d.lodAssets) {
      if (lod.empty()) continue;
      if (!firstLod) out << ',';
      firstLod = false;
      out << std::quoted(std::string(lod));
    }
    out << "]"
        << ",\"liveries\":[";
    if (d.alternateLivery) {
      const auto& livery = *d.alternateLivery;
      out << "{\"id\":" << std::quoted(std::string(livery.key))
          << ",\"name\":" << std::quoted(std::string(livery.name))
          << ",\"model\":" << std::quoted(std::string(livery.modelAsset)) << ",\"lods\":[";
      bool firstLivery = true;
      for (const auto& lod : livery.lodAssets) {
        if (lod.empty()) continue;
        out << (firstLivery ? "" : ",") << std::quoted(std::string(lod));
        firstLivery = false;
      }
      out << "]}";
    }
    out << "]}";
  }
  out << "]}\n";
  return out ? 0 : 1;
}
