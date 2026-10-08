#pragma once
// Procedural vegetation in renderer axes: X east, Y up, Z south.
#include "ofs/math.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
namespace ofs::client {
struct SurfaceVertex {
  float x,y,z,nx,ny,nz,u,v;
  float tx{},ty{},tz{},tw{};
};
inline float sceneryRandom(std::uint32_t seed) {
  seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;
  return float(seed&0xffffff)/float(0xffffff);
}
inline void sceneryTriangle(std::vector<SurfaceVertex>& out,Vec3 a,Vec3 b,Vec3 c,
                            Vec3 na,Vec3 nb,Vec3 nc,float variation) {
  const auto face=(b-a).cross(c-a);
  if (face.norm2()<1e-14) return;
  if (face.dot(na+nb+nc)<0) {std::swap(b,c);std::swap(nb,nc);}
  const auto emit=[&](Vec3 p,Vec3 n){out.push_back({float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),variation,0});};
  emit(a,na);emit(b,nb);emit(c,nc);
}
inline void foliageLobe(std::vector<SurfaceVertex>& out,Vec3 center,Vec3 radii,
                        unsigned seed,int sides,int rings) {
  // Closed ellipsoid with softly scalloped rings, smooth outward normals.
  const auto point=[&](int ring,int side){
    const double latitude=-kPi*.5+kPi*ring/rings,angle=2*kPi*side/sides;
    const double ripple=1+(.09*std::sin(angle*3+seed*.13)+.05*std::sin(angle*5+latitude*4+seed*.29))*std::cos(latitude);
    const Vec3 sphere{std::cos(latitude)*std::cos(angle)*ripple,std::sin(latitude),std::cos(latitude)*std::sin(angle)*ripple};
    const Vec3 p=center+Vec3{sphere.x*radii.x,sphere.y*radii.y,sphere.z*radii.z};
    const Vec3 n=Vec3{sphere.x/radii.x,sphere.y/radii.y,sphere.z/radii.z}.normalized();
    return std::pair{p,n};
  };
  for(int r=0;r<rings;++r) for(int s=0;s<sides;++s) {
    const auto [a,na]=point(r,s);const auto [b,nb]=point(r,s+1);
    const auto [c,nc]=point(r+1,s+1);const auto [d,nd]=point(r+1,s);
    const float shade=sceneryRandom(seed+19);
    sceneryTriangle(out,a,b,c,na,nb,nc,shade);sceneryTriangle(out,a,c,d,na,nc,nd,shade);
  }
}
inline void treeBranch(std::vector<SurfaceVertex>& wood,Vec3 a,Vec3 b,double radius,unsigned seed) {
  const auto axis=(b-a).normalized();const auto side=axis.cross({1,0,0}).normalized();
  const auto up=axis.cross(side).normalized();
  for(int s=0;s<6;++s) {
    const double p=2*kPi*s/6,q=2*kPi*(s+1)/6;
    const auto n=side*std::cos(p)+up*std::sin(p),m=side*std::cos(q)+up*std::sin(q);
    sceneryTriangle(wood,a+n*radius,a+m*radius,b+m*(radius*.55),n,m,m,sceneryRandom(seed));
    sceneryTriangle(wood,a+n*radius,b+m*(radius*.55),b+n*(radius*.55),n,m,n,sceneryRandom(seed));
  }
}

// One closed cone tier with a flat underside, used for conifer boughs.
inline void foliageCone(std::vector<SurfaceVertex>& out,Vec3 base,double radius,double height,
                        unsigned seed,int sides) {
  const Vec3 tip=base+Vec3{0,height,0};
  for(int s=0;s<sides;++s) {
    const double a=2*kPi*s/sides,b=2*kPi*(s+1)/sides;
    // A little irregularity keeps a stand of conifers from looking turned on a lathe.
    const double ra=radius*(1+(sceneryRandom(seed+unsigned(s))-.5)*.22);
    const double rb=radius*(1+(sceneryRandom(seed+unsigned((s+1)%sides))-.5)*.22);
    const Vec3 p=base+Vec3{std::cos(a)*ra,0,std::sin(a)*ra},q=base+Vec3{std::cos(b)*rb,0,std::sin(b)*rb};
    const auto na=Vec3{std::cos(a),radius/height,std::sin(a)}.normalized();
    const auto nb=Vec3{std::cos(b),radius/height,std::sin(b)}.normalized();
    sceneryTriangle(out,p,q,tip,na,nb,(na+nb).normalized(),0);
    sceneryTriangle(out,base,q,p,{0,-1,0},{0,-1,0},{0,-1,0},0);
  }
}

// A tree of unit height standing on the origin, for instanced drawing: the
// renderer scales it per instance. Texture coordinate u is 0 for wood and 1 for
// foliage; v is the height within the tree, which the shader uses to darken the
// inside of the crown. `distant` selects the reduced mesh used beyond about a
// kilometre.
inline std::vector<SurfaceVertex> unitTree(bool conifer,bool distant) {
  std::vector<SurfaceVertex> wood,leaves;
  const unsigned seed=conifer?9101u:7703u;
  if(conifer) {
    treeBranch(wood,{0,0,0},{0,.9,0},.018,seed);
    if(distant) {
      foliageCone(leaves,{0,.16,0},.24,.50,seed+1,6);
      foliageCone(leaves,{0,.46,0},.16,.54,seed+2,6);
    } else {
      for(int tier=0;tier<5;++tier) {
        const double t=double(tier)/5;
        foliageCone(leaves,{0,.16+.75*t,0},.25*(1-.72*t),.30-.06*t,seed+unsigned(tier)*17,9);
      }
    }
  } else {
    treeBranch(wood,{0,0,0},{0,.50,0},.026,seed);
    if(distant) {
      foliageLobe(leaves,{0,.64,0},{.29,.34,.29},seed,6,3);
    } else {
      // A crown is a heap of boughs, not a ball: a core, limbs reaching out
      // from the fork, and smaller masses breaking the outline all over it.
      foliageLobe(leaves,{0,.64,0},{.22,.25,.22},seed,8,5);
      foliageLobe(leaves,{.02,.86,.01},{.15,.13,.15},seed+3,6,4);
      for(unsigned branch=0;branch<5;++branch) {
        const double angle=branch*1.2566+.6+sceneryRandom(seed+branch)*.5;
        const double reach=.17+sceneryRandom(seed+branch+9)*.05;
        const Vec3 end{std::cos(angle)*reach,.47+sceneryRandom(seed+branch+5)*.12,std::sin(angle)*reach};
        treeBranch(wood,{0,.34,0},end,.010,seed+branch);
        foliageLobe(leaves,end,{.150,.125,.150},seed+branch*19,6,4);
      }
      for(unsigned mass=0;mass<7;++mass) {
        const double angle=mass*.8976+sceneryRandom(seed+mass+31)*.7;
        const double rise=.56+sceneryRandom(seed+mass+47)*.30;
        const double reach=(.20+sceneryRandom(seed+mass+53)*.05)*std::sqrt(std::max(.2,1-std::pow((rise-.62)/.34,2)));
        const double size=.085+sceneryRandom(seed+mass+61)*.035;
        foliageLobe(leaves,{std::cos(angle)*reach,rise,std::sin(angle)*reach},{size,size*.85,size},seed+mass*23+7,5,3);
      }
    }
  }
  std::vector<SurfaceVertex> out;
  out.reserve(wood.size()+leaves.size());
  for(auto v:wood) {v.u=0;v.v=v.y;out.push_back(v);}
  for(auto v:leaves) {v.u=1;v.v=v.y;out.push_back(v);}
  return out;
}
} // namespace ofs::client
