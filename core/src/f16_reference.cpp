#include "ofs/f16_reference.hpp"
#include "f16_imported.hpp"
#include <stdexcept>
namespace ofs {
namespace {
class NasaF16Aero final : public AerodynamicModel {
public:
  AeroCoefficients coefficients(const AeroInputs& i) const override {
    for(double v:{i.alpha,i.beta,i.mach,i.phat,i.qhat,i.rhat,i.elevator,i.aileron,i.rudder,i.leading_edge,i.trailing_edge,i.speed_brake,i.configuration})
      if(!std::isfinite(v))throw std::domain_error("Nonfinite F-16 input");
    // This subsonic database has no Mach/device/configuration dependence.
    // Reject unsupported configurations rather than adding invented corrections.
    if(i.mach<0 || i.mach>1 || i.leading_edge!=0 || i.trailing_edge!=0 || i.speed_brake!=0 || i.configuration!=0)
      throw std::out_of_range("NASA F-16 model requires clean subsonic configuration");
    const double alpha=clamp(i.alpha,-10*kDeg2Rad,45*kDeg2Rad);
    const double beta=clamp(i.beta,-30*kDeg2Rad,30*kDeg2Rad);
    const double el=clamp(i.elevator,-24*kDeg2Rad,24*kDeg2Rad);
    const double ail=i.aileron/(20*kDeg2Rad);
    const double rdr=i.rudder/(30*kDeg2Rad);
    const std::array a{alpha};const std::array ea{el,alpha},ba{std::abs(beta),alpha},signedBa{beta,alpha};
    const double sign=(beta>0)-(beta<0);
    using namespace f16_data;
    // Source-described odd beta symmetry applies only to static Cl/Cn.
    return {cxt().evaluate(ea)+i.qhat*cxq().evaluate(a),
      -.02*beta*kRad2Deg+.021*ail+.086*rdr+i.phat*cyp().evaluate(a)+i.rhat*cyr().evaluate(a),
      czt().evaluate(a)*(1-beta*beta)-.19*el/(25*kDeg2Rad)+i.qhat*czq().evaluate(a),
      sign*absCl0().evaluate(ba)+dclda().evaluate(signedBa)*ail+dcldr().evaluate(signedBa)*rdr+i.phat*clp().evaluate(a)+i.rhat*clr().evaluate(a),
      cmt().evaluate(ea)+i.qhat*cmq().evaluate(a),
      sign*absCn0().evaluate(ba)+dcnda().evaluate(signedBa)*ail+dcndr().evaluate(signedBa)*rdr+i.phat*cnp().evaluate(a)+i.rhat*cnr().evaluate(a)};
  }
};
}
std::shared_ptr<const AerodynamicModel> f16ReferenceAero() {
  static const auto model=std::make_shared<const NasaF16Aero>();return model;
}
std::shared_ptr<const PropulsionModel> f16ReferenceEngine() {
  static const auto model=[] {
    std::vector<double> values;
    for(int alt=0;alt<6;++alt)for(int mach=0;mach<6;++mach) {
      const std::array point{mach*.2,alt*10000*.3048};
      values.push_back(f16_data::T_IDLE().evaluate(point));
      values.push_back(f16_data::T_MIL().evaluate(point));
      values.push_back(f16_data::T_MAX().evaluate(point));
    }
    return std::make_shared<const EngineDeckModel>(GridTable({{0,3048,6096,9144,12192,15240},{0,.2,.4,.6,.8,1},{0,.5,1}},std::move(values)));
  }();return model;
}
AircraftConfig f16ReferenceConfig() {
  AircraftConfig c;
  c.aero_kind=AeroModelKind::DataDriven;c.jet_kind=JetModelKind::EngineDeck;
  c.aerodynamic_model=f16ReferenceAero();c.propulsion_model=f16ReferenceEngine();
  c.provenance_dataset="nasa-nesc-f16-import";
  c.wing_area=f16_data::sref;c.wing_span=f16_data::bspan;c.mac=f16_data::CBAR;
  c.mass=c.empty_mass=f16_data::XMASS;c.initial_fuel=0;c.initial_payload=0;
  // Fixed-mass NASA validation model: no fuel-flow data exists in this deck.
  // One positive inventory marker keeps the engine enabled without consumption.
  c.initial_fuel=1;c.empty_mass=c.mass-1;c.fuel_position={};c.fuel_flow_scale=0;
  c.ixx=f16_data::XIXX;c.iyy=f16_data::XIYY;c.izz=f16_data::XIZZ;
  c.ixz=-f16_data::XIZX;c.ixy=-f16_data::XIXY;c.iyz=-f16_data::XIYZ;
  c.control_law=FlightControlLaw::Direct;c.engine_count=1;
  c.thrust_sl_static_each=12680*4.4482216152605;c.afterburner_thrust_each=20000*4.4482216152605;
  c.afterburner_threshold=.5;c.engine_pos_l={};c.engine_pos_r={};
  c.elev_min=-24*kDeg2Rad;c.elev_max=24*kDeg2Rad;c.ail_max=20*kDeg2Rad;c.rud_max=30*kDeg2Rad;
  c.contacts_enabled=false; // no NASA landing-gear dataset was supplied
  c.flap_max_deg=0;c.table_aileron_sign=-1; // NASA positive deflection is left roll
  configureSurfaces(c);return c;
}
}
