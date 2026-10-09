// Published figures each armed aircraft is set from, checked against what the
// simulator does with them. See docs/AIRCRAFT_REFERENCE.md.
#include "scenario.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/weapons.hpp"
#include <cstdio>
using namespace ofs;
using scenario::check;
namespace {
// Thrust less drag in level flight at full power, clean, at the reference mass.
bool levelExcess(const AircraftConfig& config,double altitude,double tas,bool reheat,double& excess) {
  double low=-2*kDeg2Rad,high=20*kDeg2Rad;
  for(int i=0;i<40;++i) {
    const double alpha=.5*(low+high);
    State s;s.pos_ned={0,0,-altitude};s.att=quatFromEuler(0,alpha,0);s.vel_ned={tas,0,0};
    s.n1[0]=s.n1[1]=1;s.afterburner[0]=s.afterburner[1]=reheat?1:0;
    Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=1;
    Simulator sim(config);sim.setControls(c);check(sim.setState(s),"level state accepted");
    const Vec3 force=sim.state().att.rotate(sim.evalAero().force_body+sim.evalThrust().force_body);
    if(-force.z<sim.massProperties().mass*kG0)low=alpha;else high=alpha;
    excess=force.x;
  }
  return high<19.9*kDeg2Rad;
}
// Highest Mach number reached by accelerating from low speed: the scan stops
// at the first speed the aircraft cannot hold, so a drag rise it cannot cross
// is a limit even if thrust would exceed drag again beyond it.
double topMach(const AircraftConfig& config,double altitude,bool reheat) {
  const double sound=isaAtAltitude(altitude).sound;double best=0;bool flying=false;
  for(double mach=.3;mach<4;mach+=.01) {
    double excess;const bool holds=levelExcess(config,altitude,mach*sound,reheat,excess)&&excess>0;
    if(holds){best=mach;flying=true;}else if(flying)break;
  }
  return best;
}
struct Reference {
  AircraftType type;
  double emptyKg,fuelKg,areaM2,dryN,reheatN,loadLimit;
  double machAltitude,machSeaLevel,ceilingBelowM; // machSeaLevel 0: none published
  bool supercruise;
  double rpm,muzzle;unsigned rounds,heatSeekers,radarMissiles;
};
void near(double value,double target,double tolerance,const char* what){check(std::abs(value-target)<=tolerance,what);}
}
int main() {
  try {
    {
      // The bomber: published masses and thrust, and its three loads.
      const auto& d=aircraftDefinition(AircraftType::B52);const auto& f=d.flight;
      check(d.bomber && !d.gun && f.empty_mass==83250 && f.fuel_capacity==141600 && f.wing_area==370 && f.wing_span==56.4,"B-52 published mass, fuel and wing");
      check(f.engine_count*f.thrust_sl_static_each==8*75620. && f.afterburner_thrust_each==0,"B-52 eight TF33 without reheat");
      check(weapons::bombLoadouts(AircraftType::B52)==3 && weapons::bombLoadouts(AircraftType::Typhoon)==0,"only the bomber has bomb loads");
      const auto small=weapons::bombLoad(AircraftType::B52,0),large=weapons::bombLoad(AircraftType::B52,1),nuclear=weapons::bombLoad(AircraftType::B52,2);
      check(small.count==51 && small.type==weapons::WeaponType::Bomb500 && large.count==18 && large.type==weapons::WeaponType::Bomb2000 &&
            nuclear.count==1 && nuclear.type==weapons::WeaponType::Nuclear,"B-52 loads");
      weapons::Inventory bay;bay.reset(AircraftType::B52,1);
      State state;bay.applyPayload(f,state);
      check(std::abs(state.payload_mass-18*925.)<1e-6 && bay.stations.empty(),"bombs are carried as payload");
      // A bomb let go in level flight lands ahead of the release point, and
      // the drag-heavy weapon falls shorter and slower than the slick one.
      Vec3 slick,heavy;double slickTime,heavyTime;
      check(weapons::bombImpact(weapons::bombDefinition(weapons::WeaponType::Bomb500),{0,0,-6000},{200,0,0},{},slick,slickTime) &&
            weapons::bombImpact(weapons::bombDefinition(weapons::WeaponType::Nuclear),{0,0,-6000},{200,0,0},{},heavy,heavyTime),"bombs reach the ground");
      check(slick.x>5500 && slick.x<7000 && slickTime>34 && slickTime<38 && heavy.x<slick.x && heavyTime>slickTime+5,"bomb trajectories");
      std::printf("B-52 Mk 82 from 6 km at 200 m/s: %.0f m downrange in %.1f s; B83: %.0f m in %.1f s\n",slick.x,slickTime,heavy.x,heavyTime);
    }
    const Reference references[]{
      {AircraftType::Typhoon,11000,4996,51.2,60000,90000,9,2.0,1.25,22000,true,1700,1025,150,2,4},
      {AircraftType::Su57,18500,10300,78.8,93000,147000,9,2.0,1.10,22000,true,1500,860,150,2,4},
      {AircraftType::JF17,6586,2330,24.43,49400,84400,8,1.6,0,20000,false,3400,715,200,2,4}};
    for(const auto& r:references) {
      if(!validAircraftType(r.type))continue;
      const auto& d=aircraftDefinition(r.type);const auto& f=d.flight;const char* key=d.key.data();
      check(f.empty_mass==r.emptyKg && f.fuel_capacity==r.fuelKg && f.wing_area==r.areaM2,"published mass, fuel and wing area");
      check(f.initial_fuel<=f.fuel_capacity && f.mass==f.empty_mass+f.initial_fuel+f.initial_payload,"spawn mass is empty mass plus its load");
      check(f.thrust_sl_static_each==r.dryN && f.afterburner_thrust_each==r.reheatN && f.g_positive==r.loadLimit,"published thrust and load limit");
      check(d.gun && d.gun->rpm==r.rpm && d.gun->muzzleVelocity==r.muzzle && d.gun->ammo==r.rounds,"published gun");
      weapons::Inventory loadout;loadout.reset(r.type);
      check(loadout.remaining(weapons::WeaponType::Infrared)==r.heatSeekers &&
            loadout.remaining(weapons::WeaponType::ActiveRadar)==r.radarMissiles,"usual air-to-air load");
      for(const auto& station:loadout.stations)
        check(std::abs(station.position.y)<=.5*f.wing_span+.2 && std::abs(station.position.x)<12,"station on the airframe");
      const double high=topMach(f,11000,true),dry=topMach(f,11000,false),low=topMach(f,100,true);
      std::printf("%s top speed Mach %.2f at 11 km (published %.2f), %.2f without reheat, %.2f at sea level\n",key,high,r.machAltitude,dry,low);
      near(high,r.machAltitude,.08,"published top speed at altitude");
      if(r.machSeaLevel>0)near(low,r.machSeaLevel,.06,"published top speed at sea level");
      check(r.supercruise?dry>1.2:dry<=1.02,"supersonic without reheat only where the type can");
      // No level flight 2 to 3 km above the published service ceiling.
      double excess;bool holds=false;const double sound=isaAtAltitude(r.ceilingBelowM).sound;
      for(double mach=.5;mach<2.6;mach+=.02)holds|=levelExcess(f,r.ceilingBelowM,mach*sound,true,excess)&&excess>0;
      check(!holds,"a ceiling exists");
      check(topMach(f,15000,true)<=high+.02,"top speed does not keep rising above the tropopause");
    }
    std::puts("PASS aircraft reference figures");
    return 0;
  } catch(const std::exception& error) {
    std::fprintf(stderr,"AIRCRAFT REFERENCE FAIL: %s\n",error.what());
    return 1;
  }
}
