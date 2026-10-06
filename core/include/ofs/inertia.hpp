#pragma once
#include "ofs/math.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ofs {
// Entries of the inertia matrix, not the unsigned aerospace products of inertia.
// In particular the matrix X/Z entry is -Jxz when a source publishes Jxz.
struct InertiaTensor {
  double xx{}, yy{}, zz{}, xy{}, xz{}, yz{};
  Vec3 apply(Vec3 v) const {
    return {xx*v.x+xy*v.y+xz*v.z, xy*v.x+yy*v.y+yz*v.z,
            xz*v.x+yz*v.y+zz*v.z};
  }
  double determinant() const {
    return xx*(yy*zz-yz*yz)-xy*(xy*zz-yz*xz)+xz*(xy*yz-yy*xz);
  }
  bool positiveDefinite() const {
    const double scale=std::max({std::abs(xx),std::abs(yy),std::abs(zz),std::abs(xy),std::abs(xz),std::abs(yz)});
    if (!std::isfinite(scale) || scale<=0) return false;
    for(double v:{xx,yy,zz,xy,xz,yz}) if(!std::isfinite(v))return false;
    const double a=xx/scale,b=yy/scale,c=zz/scale,d=xy/scale,e=xz/scale,f=yz/scale;
    return a>0 && a*b-d*d>0 && a*(b*c-f*f)-d*(d*c-e*f)+e*(d*f-b*e)>0;
  }
  // Jacobi eigenvectors of the symmetric tensor; stable for diagonal/repeated
  // eigenvalues as well. Axes are unsorted because sign/order is arbitrary.
  std::array<Vec3,3> principalAxes() const {
    double a[3][3]{{xx,xy,xz},{xy,yy,yz},{xz,yz,zz}};
    double v[3][3]{{1,0,0},{0,1,0},{0,0,1}};
    for(unsigned iteration=0;iteration<24;++iteration) {
      unsigned p=0,q=1;
      for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)
        if(std::abs(a[i][j])>std::abs(a[p][q])){p=i;q=j;}
      if(std::abs(a[p][q])<1e-12*std::max({std::abs(xx),std::abs(yy),std::abs(zz)}))break;
      const double angle=.5*std::atan2(2*a[p][q],a[q][q]-a[p][p]);
      const double c=std::cos(angle),s=std::sin(angle);
      const double pp=a[p][p],qq=a[q][q],pq=a[p][q];
      a[p][p]=c*c*pp-2*s*c*pq+s*s*qq;
      a[q][q]=s*s*pp+2*s*c*pq+c*c*qq;a[p][q]=a[q][p]=0;
      for(unsigned k=0;k<3;++k) {
        if(k!=p&&k!=q){const double kp=a[k][p],kq=a[k][q];
          a[k][p]=a[p][k]=c*kp-s*kq;a[k][q]=a[q][k]=s*kp+c*kq;}
        const double vp=v[k][p],vq=v[k][q];v[k][p]=c*vp-s*vq;v[k][q]=s*vp+c*vq;
      }
    }
    return {{{v[0][0],v[1][0],v[2][0]},{v[0][1],v[1][1],v[2][1]},{v[0][2],v[1][2],v[2][2]}}};
  }
  // Cholesky solve avoids forming a poorly scaled cofactor inverse.
  Vec3 solve(Vec3 v) const {
    if(!positiveDefinite())throw std::domain_error("Inertia tensor must be positive definite");
    const double a=std::sqrt(xx), b=xy/a, c=xz/a;
    const double pivotY=yy-b*b;
    if(!std::isfinite(pivotY)||pivotY<=0)throw std::domain_error("Ill-conditioned inertia Y pivot");
    const double d=std::sqrt(pivotY), e=(yz-b*c)/d;
    const double pivotZ=zz-c*c-e*e;
    if(!std::isfinite(pivotZ)||pivotZ<=0)throw std::domain_error("Ill-conditioned inertia Z pivot");
    const double f=std::sqrt(pivotZ);
    const double u=v.x/a, w=(v.y-b*u)/d, t=(v.z-c*u-e*w)/f;
    const double z=t/f, y=(w-e*z)/d;
    return {(u-b*y-c*z)/a,y,z};
  }
};
}
