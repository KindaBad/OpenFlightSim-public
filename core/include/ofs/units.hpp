#pragma once
// Named conversions at UI, external-reference and tooling boundaries.
namespace ofs::units {
inline constexpr double metrePerFoot = .3048;
inline constexpr double metrePerNauticalMile = 1852;
inline constexpr double kgPerPound = .45359237;
inline constexpr double newtonPerPoundForce = 4.4482216152605;
inline constexpr double kgMetreSquaredPerSlugFootSquared = 1.3558179483314;
constexpr double feetToMetres(double feet) { return feet * metrePerFoot; }
constexpr double metresToFeet(double metres) { return metres / metrePerFoot; }
constexpr double knotsToMetresPerSecond(double knots) { return knots * metrePerNauticalMile / 3600; }
constexpr double metresPerSecondToKnots(double speed) { return speed * 3600 / metrePerNauticalMile; }
constexpr double poundsToKg(double pounds) { return pounds * kgPerPound; }
constexpr double poundsForceToNewtons(double force) { return force * newtonPerPoundForce; }
constexpr double squareFeetToSquareMetres(double area) { return area * metrePerFoot * metrePerFoot; }
} // namespace ofs::units
