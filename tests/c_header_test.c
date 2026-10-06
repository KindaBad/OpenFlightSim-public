#include "ofs/c_api.h"
int main(void) {
  OfsSim* sim = ofs_create();
  if (!sim) return 1;
  ofs_step(sim, 1.0 / 120.0);
  const OfsState s = ofs_get_state(sim);
  ofs_destroy(sim);
  return s.time > 0.0 ? 0 : 1;
}
