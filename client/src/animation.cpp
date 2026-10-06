#include "animation.hpp"
#include "ofs/airliner.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace ofs::client {
namespace {
AssetMatrix identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }
double approach(double value, double target, double step) {
  return value + std::clamp(target - value, -step, step);
}
AssetMatrix rotation(const std::array<float, 3>& inputAxis, double angle) {
  const Vec3 axis = Vec3{inputAxis[0], inputAxis[1], inputAxis[2]}.normalized();
  const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
  const double x = axis.x, y = axis.y, z = axis.z;
  return {float(t*x*x+c), float(t*x*y+s*z), float(t*x*z-s*y), 0,
          float(t*x*y-s*z), float(t*y*y+c), float(t*y*z+s*x), 0,
          float(t*x*z+s*y), float(t*y*z-s*x), float(t*z*z+c), 0,
          0,0,0,1};
}
}

AssetMatrix multiplyAsset(const AssetMatrix& a, const AssetMatrix& b) {
  AssetMatrix result{};
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k) result[column*4+row] += a[k*4+row]*b[column*4+k];
  return result;
}
AssetMatrix inverseAsset(const AssetMatrix& a) {
  const double det = a[0]*(a[5]*a[10]-a[9]*a[6]) - a[4]*(a[1]*a[10]-a[9]*a[2]) +
                     a[8]*(a[1]*a[6]-a[5]*a[2]);
  if (std::abs(det) < 1e-12) throw std::runtime_error("singular glTF node transform");
  AssetMatrix r = identity();
  r[0]=(a[5]*a[10]-a[9]*a[6])/det; r[4]=(a[8]*a[6]-a[4]*a[10])/det; r[8]=(a[4]*a[9]-a[8]*a[5])/det;
  r[1]=(a[9]*a[2]-a[1]*a[10])/det; r[5]=(a[0]*a[10]-a[8]*a[2])/det; r[9]=(a[8]*a[1]-a[0]*a[9])/det;
  r[2]=(a[1]*a[6]-a[5]*a[2])/det; r[6]=(a[4]*a[2]-a[0]*a[6])/det; r[10]=(a[0]*a[5]-a[4]*a[1])/det;
  for (int row=0;row<3;++row) r[12+row]=-(r[row]*a[12]+r[4+row]*a[13]+r[8+row]*a[14]);
  return r;
}

void AircraftPose::update(const State& state, const Controls& c, const AircraftDefinition& definition, double dt) {
  const auto& v = definition.visual;
  const auto& f = definition.flight;
  deltaSurfaces=f.control_law==FlightControlLaw::Delta;
  for(unsigned e=0;e<2;++e) inlet[e]=f.variable_inlets?state.inlet_spike[e]:0;
  for(unsigned e=0;e<2;++e) vectorAngle[e]=state.nozzle_angle[e];
  const auto cg=loadedCg(f,state);
  const double h = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0;
  if (!primed) { gear=c.gear01; flap=c.flap01; spoiler=c.spoiler01; primed=true; }
  gear = approach(gear, c.gear01, h / v.gearSeconds);
  flap = state.actuators_initialized ? state.flap : approach(flap, c.flap01, h / v.flapSeconds);
  airlinerSurfaces=f.aero_kind==AeroModelKind::AirlinerEngineering;
  const auto highlift=a320HighLift(flap);
  flapAngle=highlift.flap_degrees;slatAngle=highlift.slat_degrees*kDeg2Rad;
  spoiler = state.actuators_initialized ? state.spoiler : approach(spoiler, c.spoiler01, h / .6);
  const auto compression=[&](const Vec3& contact){
    return clamp((state.pos_ned+state.att.rotate(contact-cg)).z,0,f.oleo_stroke)*gear;
  };
  compressionNose=compression(f.gear_nose);
  compressionMain[0]=compression(f.gear_main_l);
  compressionMain[1]=compression(f.gear_main_r);
  const double speed = state.vel_ned.norm();
  const double steerMax = lerp(70*kDeg2Rad, 6*kDeg2Rad, clamp(speed/60,0,1));
  steering = approach(steering, -c.steering*steerMax*gear, h*2);
  if (-state.pos_ned.z < f.gear_nose.z + .5 && gear > .95) {
    const double forward = state.att.inverseRotate(state.vel_ned).x;
    wheel = std::remainder(wheel + forward*h/v.wheelRadius, 2*kPi);
    noseWheel = std::remainder(noseWheel + forward*h/v.noseWheelRadius, 2*kPi);
  }
  for (unsigned e=0;e<f.engine_count;++e)
    fan[e]=std::remainder(fan[e]+h*state.n1[e]*160, 2*kPi);
  for (unsigned e=0;e<2;++e)
    nozzle[e]=approach(nozzle[e],state.afterburner[e],h*3.5);
  aileron = (state.actuators_initialized?state.aileron:c.aileron_stick)*f.ail_max;
  const double effective = state.actuators_initialized?state.elevator:clamp(c.elevator_stick+c.elevator_trim,-1,1);
  const double de = effective>=0 ? effective*f.elev_min : -effective*f.elev_max;
  elevator = -de;
  levcon=-effective*f.levcon_max;
  rudder = (state.actuators_initialized?state.rudder:c.rudder_pedal)*f.rud_max;
  physicalSurfaces=state.actuators_initialized;
  canard=-elevatorDeflection(state.canard,f);
  elevonLeft=elevatorDeflection(state.elevon_l,f)-state.flap*15*kDeg2Rad;
  elevonRight=elevatorDeflection(state.elevon_r,f)-state.flap*15*kDeg2Rad;
}

double AircraftPose::channel(std::string_view name) const {
  if (name=="levcon") return levcon;
  if (name=="slat") return airlinerSurfaces?-slatAngle:-flap*12*kDeg2Rad;
  if (name=="vector_L") return vectorAngle[0];
  if (name=="vector_R") return vectorAngle[1];
  if (name=="aileron_L") return -aileron;
  if (name=="aileron_R") return aileron;
  if (name=="elevator") return elevator;
  if (name=="canard") return physicalSurfaces?canard:elevator;
  if (name=="elevon_L") return clamp(physicalSurfaces?elevonLeft:-elevator+(deltaSurfaces?1:-1)*aileron-flap*15*kDeg2Rad,(deltaSurfaces?-35:-25)*kDeg2Rad,25*kDeg2Rad);
  if (name=="elevon_R") return clamp(physicalSurfaces?elevonRight:-elevator+(deltaSurfaces?-1:1)*aileron-flap*15*kDeg2Rad,(deltaSurfaces?-35:-25)*kDeg2Rad,25*kDeg2Rad);
  if (name=="rudder") return rudder;
  if (name=="flap") return airlinerSurfaces?flapAngle/35:flap;
  if (name=="spoiler") return spoiler;
  if (name=="gear") return 1-gear;
  if (name=="gear_fold") {
    const double t=clamp((1-gear-.12)/.78,0,1);
    return t*t*(3-2*t);
  }
  if (name=="gear_door") {
    const double t=clamp(gear/.12,0,1);
    return 1-t*t*(3-2*t);
  }
  if (name=="steering") return steering;
  if (name=="wheel") return wheel;
  if (name=="nose_wheel") return noseWheel;
  if (name=="compression_nose") return compressionNose;
  if (name=="compression_L") return compressionMain[0];
  if (name=="compression_R") return compressionMain[1];
  if (name=="fan_L") return fan[0];
  if (name=="fan_R") return fan[1];
  if (name=="nozzle_L") return nozzle[0];
  if (name=="nozzle_R") return nozzle[1];
  if (name=="inlet_L") return inlet[0];
  if (name=="inlet_R") return inlet[1];
  return 0;
}

void evaluatePose(const std::vector<GltfNode>& nodes, const AircraftPose& pose, std::vector<AssetMatrix>& deltas) {
  deltas.resize(nodes.size());
  const auto visit = [&](auto&& self, int index, const AssetMatrix& parent) -> void {
    const auto& node = nodes[index];
    if (!node.active) return;
    if (node.channel.empty() && node.children.empty()) return;
    const double value = pose.channel(node.channel);
    auto movement = rotation(node.axis, value*node.gain);
    for (int i=0;i<3;++i) movement[12+i]=node.slide[i]*value;
    const auto world = multiplyAsset(parent, multiplyAsset(node.local, movement));
    deltas[index] = multiplyAsset(world, inverseAsset(node.world));
    for (const int child : node.children) self(self,child,world);
  };
  for (std::size_t i=0;i<nodes.size();++i)
    if (nodes[i].parent<0) visit(visit,static_cast<int>(i),identity());
}
}
