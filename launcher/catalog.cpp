// Export the actual compiled registry. Neither the launcher nor release tooling
// maintains a second list of supported aircraft or claims unavailable modes.
#include "ofs/aircraft_definition.hpp"
#include "ofs_version.hpp"
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
  out << "],\"aircraft\":[";
  bool first = true;
  for (const auto& d : ofs::aircraftDefinitions()) {
    if (!first) out << ',';
    first = false;
    out << "{\"id\":" << std::quoted(std::string(d.key))
        << ",\"name\":" << std::quoted(std::string(d.displayName))
        << ",\"model\":" << std::quoted(std::string(d.modelAsset))
        << ",\"armed\":" << (d.gun ? "true" : "false")
        << ",\"engines\":" << d.flight.engine_count
        << ",\"span_m\":" << d.flight.wing_span
        << ",\"reference_mass_kg\":" << d.flight.mass << ",\"lods\":[";
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
