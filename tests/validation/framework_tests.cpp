#include "case.hpp"
int main() {try {
  using scenario::check;using validation::compare;
  check(std::string_view(compare(12,10,11,.5,true).status)=="FAIL","out-of-tolerance comparable case fails");
  check(std::string_view(compare(10.2,10,10,.5,true).status)=="PASS","predeclared uncertainty applies");
  check(std::string_view(compare(10,10,10,0,false).status)=="WARN","coincidental agreement cannot validate mismatched case");
  check(compare(12,10,11,0,true).absoluteError==1,"error measured to nearest interval endpoint");
  bool rejected=false;try{compare(std::nan(""),1,1,0,true);}catch(const std::runtime_error&){rejected=true;}check(rejected,"NaN cannot pass");
  rejected=false;try{validation::requireMetadata({});}catch(const std::runtime_error&){rejected=true;}check(rejected,"incomplete metadata rejected");
  return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL framework: %s\n",e.what());return 1;}}
