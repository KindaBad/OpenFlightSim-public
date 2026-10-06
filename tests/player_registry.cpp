#include "ofs/aircraft_definition.hpp"
#include <cstdio>
#include <stdexcept>

int main() {
  using namespace ofs;
  try {
    const auto expected = OFS_INCLUDE_SU57 ? 4u : 3u;
    if (aircraftDefinitions().size() != expected ||
        validAircraftType(AircraftType::Su57) != bool(OFS_INCLUDE_SU57))
      throw std::runtime_error("Player aircraft registry differs from configured content");
    for (const auto type : {AircraftType::A320, AircraftType::Typhoon, AircraftType::SR71}) {
      const auto& definition = aircraftDefinition(type);
      if (aircraftTypeFromName(definition.key) != type)
        throw std::runtime_error("Original player aircraft is missing");
    }
    if (!OFS_INCLUDE_SU57) {
      bool rejected = false;
      try { (void)aircraftTypeFromName("su57"); }
      catch (const std::invalid_argument&) { rejected = true; }
      if (!rejected) throw std::runtime_error("Excluded aircraft can still be selected");
    }
    const auto fighter = OFS_INCLUDE_SU57 ? AircraftType::Su57 : AircraftType::Typhoon;
    if (dogfightAircraftType(AircraftType::A320) != fighter ||
        dogfightAircraftType(AircraftType::SR71) != fighter ||
        dogfightAircraftType(AircraftType::Typhoon) != AircraftType::Typhoon)
      throw std::runtime_error("Dogfight selection requires an available armed aircraft");
    std::printf("PASS player registry: %zu aircraft, Su-57=%d\n", aircraftDefinitions().size(), OFS_INCLUDE_SU57);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL player registry: %s\n", error.what());
    return 1;
  }
}
