#include "ofs/net/world.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
int main() {
  using namespace ofs::net;
  std::puts(
      "aircraft,tick_mean_us,tick_p95_us,tick_max_us,snapshot_mean_us,encode_"
      "mean_us,decode_mean_us,snapshot_bytes,broadcast_payload_MBps,player_value_bytes");
  for (unsigned count : {2, 8, 16, 32, 64}) {
    World world;
    for (unsigned i = 0; i < count; ++i)
      world.join();
    std::vector<double> samples;
    double snapshotUs = 0, encodeUs = 0, decodeUs = 0;
    std::size_t bytes = 0;
    unsigned snapshots = 0;
    for (unsigned t = 0; t < 7200; ++t) {
      world.step();
      samples.push_back(world.stats().lastTickUs);
      if (t % 5 == 0) {
        auto start = std::chrono::steady_clock::now();
        auto m = world.snapshot();
        auto middle = std::chrono::steady_clock::now();
        auto b = encode(m);
        auto end = std::chrono::steady_clock::now();
        snapshotUs +=
            std::chrono::duration<double, std::micro>(middle - start).count();
        encodeUs +=
            std::chrono::duration<double, std::micro>(end - middle).count();
        Message decoded;std::string reason;
        const auto decodeStart=std::chrono::steady_clock::now();
        if(!decode(b,decoded,reason)||decoded.aircrafts.size()!=count)return 1;
        decodeUs+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-decodeStart).count();
        bytes = b.size();
        ++snapshots;
      }
      for (const auto &[id, p] : world.players()) {
        (void)id;
        if (!finiteState(p.sim.state()))
          return 1;
      }
    }
    double total = 0;
    for (auto x : samples)
      total += x;
    std::sort(samples.begin(), samples.end());
    std::printf("%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%.3f,%zu\n", count,
                total / samples.size(), samples[samples.size() * 95 / 100],
                samples.back(), snapshotUs / snapshots, encodeUs / snapshots,
                decodeUs / snapshots, bytes, double(bytes) * 24 * count / 1e6,
                sizeof(Player) * count);
  }
  for(unsigned count:{1,2,4,8}) {
    Message input;input.type=Type::Input;input.entity=1;
    for(unsigned i=0;i<count;++i) {Command c;c.sequence=i+1;c.tick=i+1;input.commands.push_back(c);}
    const auto bytes=encode(input);
    Message decoded;std::string reason;if(!decode(bytes,decoded,reason)||decoded.commands.size()!=count)return 1;
    std::printf("input_commands=%u input_packet_bytes=%zu\n",count,bytes.size());
  }
  return 0;
}
