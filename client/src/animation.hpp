#pragma once
#include "gltf.hpp"
#include "ofs/aircraft_definition.hpp"

namespace ofs::client {
using AssetMatrix = std::array<float, 16>;
AssetMatrix multiplyAsset(const AssetMatrix& a, const AssetMatrix& b);
AssetMatrix inverseAsset(const AssetMatrix& matrix);

struct AircraftPose {
  double gear{1}, flap{}, spoiler{}, steering{}, wheel{}, noseWheel{}, fan[2]{};
  double aileron{}, elevator{}, rudder{}, canard{}, elevonLeft{}, elevonRight{}, levcon{};
  bool physicalSurfaces{}, airlinerSurfaces{};
  double flapAngle{}, slatAngle{};
  double nozzle[2]{};
  double inlet[2]{};
  double vectorAngle[2]{};
  bool deltaSurfaces{};
  double compressionNose{},compressionMain[2]{};
  bool primed{};
  void update(const State& state, const Controls& controls,
              const AircraftDefinition& definition, double dt);
  double channel(std::string_view name) const;
};

// Evaluates the complete tree. Each output transforms baked rest-world vertices
// to their posed asset position, including nested gear/steering/wheel parents.
void evaluatePose(const std::vector<GltfNode>& nodes, const AircraftPose& pose,
                  std::vector<AssetMatrix>& deltas);
}
