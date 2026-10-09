#include "ofs/aircraft_definition.hpp"
#include <cstdio>
#include <stdexcept>

int main() {
  using namespace ofs;
  try {
    const auto expected = 4u + OFS_INCLUDE_SU57 + OFS_INCLUDE_JF17;
    if (aircraftDefinitions().size() != expected ||
        validAircraftType(AircraftType::Su57) != bool(OFS_INCLUDE_SU57) ||
        validAircraftType(AircraftType::JF17) != bool(OFS_INCLUDE_JF17))
      throw std::runtime_error("Player aircraft registry differs from configured content");
    // The bomber is armed with bombs alone, so it is not a dogfight aircraft.
    const auto& b52 = aircraftDefinition(aircraftTypeFromName("b52"));
    if (b52.type != AircraftType::B52 || !b52.bomber || b52.gun || dogfightAircraftType(AircraftType::B52) == AircraftType::B52)
      throw std::runtime_error("B-52 registration is incomplete");
    for (const auto type : {AircraftType::A320, AircraftType::Typhoon, AircraftType::SR71, AircraftType::B52}) {
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
    if (OFS_INCLUDE_JF17) {
      // One engine in the two-slot engine state, and the published limits.
      const auto& jf17 = aircraftDefinition(aircraftTypeFromName("jf17"));
      if (jf17.type != AircraftType::JF17 || jf17.flight.engine_count != 1 || !jf17.gun ||
          dogfightAircraftType(AircraftType::JF17) != AircraftType::JF17)
        throw std::runtime_error("JF-17 registration is incomplete");
    }
    const auto fighter = OFS_INCLUDE_SU57 ? AircraftType::Su57 : AircraftType::Typhoon;
    if (dogfightAircraftType(AircraftType::A320) != fighter ||
        dogfightAircraftType(AircraftType::SR71) != fighter ||
        dogfightAircraftType(AircraftType::Typhoon) != AircraftType::Typhoon)
      throw std::runtime_error("Dogfight selection requires an available armed aircraft");
    std::printf("PASS player registry: %zu aircraft, Su-57=%d, JF-17=%d\n", aircraftDefinitions().size(), OFS_INCLUDE_SU57, OFS_INCLUDE_JF17);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL player registry: %s\n", error.what());
    return 1;
  }
}
