#pragma once
// Static procedural vegetation in renderer axes: X east, Y up, Z south.
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
inline float sceneryNoise(double x,double z) {
  const auto ix=int(std::floor(x)),iz=int(std::floor(z));
  const double fx=x-ix,fz=z-iz,a=fx*fx*(3-2*fx),b=fz*fz*(3-2*fz);
  const auto hash=[&](int dx,int dz){return sceneryRandom(std::uint32_t(ix+dx)*73856093u^std::uint32_t(iz+dz)*19349663u);};
  return float((hash(0,0)*(1-a)+hash(1,0)*a)*(1-b)+(hash(0,1)*(1-a)+hash(1,1)*a)*b);
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
    const double ripple=1+.09*std::sin(angle*3+seed*.13)*std::cos(latitude);
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
inline void appendGrassTuft(std::vector<SurfaceVertex>& out,Vec3 base,unsigned seed) {
  for(unsigned blade=0;blade<9;++blade) {
    const double angle=sceneryRandom(seed+blade*17)*2*kPi;
    const auto along=Vec3{std::cos(angle),0,std::sin(angle)};
    const auto side=Vec3{-along.z,0,along.x};
    const auto foot=base+along*(sceneryRandom(seed+blade*19)*.3);
    const double height=.22+sceneryRandom(seed+blade*31)*.40;
    const auto top=foot+Vec3{0,height,0}+along*.12;
    const auto normal=along;
    const float tint=sceneryRandom(seed+blade);
    sceneryTriangle(out,foot-side*.025,foot+side*.025,top,normal,normal,normal,tint);
    sceneryTriangle(out,foot+side*.025,foot-side*.025,top,-normal,-normal,-normal,tint);
  }
}
inline void appendTree(std::vector<SurfaceVertex>& leaves,std::vector<SurfaceVertex>& distant,
                       std::vector<SurfaceVertex>& wood,Vec3 base,double height,double width,bool pine,unsigned seed) {
  const double lean=(sceneryRandom(seed+21)-.5)*height*.06;
  const auto tip=base+Vec3{lean,height*.75,lean*.4};
  treeBranch(wood,base,tip,height*.017,seed);
  appendGrassTuft(leaves,base+Vec3{width*.8,0,width*.3},seed);
  if (pine) {
    // Overlapping irregular bough tiers replace a single smooth traffic cone.
    for(int tier=0;tier<5;++tier) {
      const double t=double(tier)/5,radius=width*(1-t*.8),bottom=height*(.17+t*.68);
      const Vec3 center=base+Vec3{lean*t,bottom,lean*t*.4};
      for(int s=0;s<9;++s) {
        const double a=2*kPi*s/9,b=2*kPi*(s+1)/9;
        const double irregular=1+(sceneryRandom(seed+unsigned(tier*37+s))-.5)*.24;
        const Vec3 p=center+Vec3{std::cos(a)*radius*irregular,0,std::sin(a)*radius*irregular};
        const Vec3 q=center+Vec3{std::cos(b)*radius,0,std::sin(b)*radius};
        const auto top=center+Vec3{0,height*.32,0};
        const auto n=Vec3{std::cos((a+b)*.5),radius/(height*.32),std::sin((a+b)*.5)}.normalized();
        sceneryTriangle(leaves,p,q,top,n,n,{0,1,0},sceneryRandom(seed+tier));
        sceneryTriangle(leaves,center,q,p,{0,-1,0},{0,-1,0},{0,-1,0},sceneryRandom(seed));
      }
    }
    // Keep recognizable tiered silhouettes in the distant mesh too.
    for(int tier=0;tier<3;++tier) {
      const double t=double(tier)/3;
      foliageLobe(distant,base+Vec3{lean*t,height*(.34+.24*t),lean*t*.4},
          {width*(1-.65*t),height*.22,width*(1-.65*t)},seed+tier,6,3);
    }
  } else {
    foliageLobe(leaves,base+Vec3{lean,height*.72,lean*.4},{width*.85,height*.27,width*.8},seed,8,5);
    foliageLobe(distant,base+Vec3{lean,height*.72,lean*.4},{width,height*.29,width*.9},seed,7,4);
    for(unsigned branch=0;branch<5;++branch) {
      const double angle=branch*2.39996+sceneryRandom(seed)*6.28;
      const double length=width*(.65+sceneryRandom(seed+branch)*.25);
      const auto end=base+Vec3{lean+std::cos(angle)*length,height*(.54+sceneryRandom(seed+branch+5)*.22),lean*.4+std::sin(angle)*length};
      treeBranch(wood,base+Vec3{lean*.5,height*.42,lean*.2},end,height*.007,seed+branch);
      foliageLobe(leaves,end,{width*.64,height*.18,width*.58},seed+branch*19,7,4);
    }
  }
}
} // namespace ofs::client
