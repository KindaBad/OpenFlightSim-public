// Export the actual compiled registry. Neither the launcher nor release tooling
// maintains a second list of supported aircraft or claims unavailable modes.
#include "ofs/aircraft_definition.hpp"
#include "ofs/weapons.hpp"
#include "ofs_version.hpp"
#ifdef OFS_NETWORK_ENABLED
#include "ofs/net/protocol.hpp"
#endif
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "ofs_catalog OUTPUT.json\n"; return 2; }
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
    out << "]}";
  }
  out << "]}\n";
  return out ? 0 : 1;
}
