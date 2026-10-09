#include "ofs/fixed_step.hpp"
#include "ofs/net/cli.hpp"
#include "ofs/net/client.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc, char **argv) {
  using namespace ofs::net;
  try {
    std::string address = "127.0.0.1", name = "bot", preset = "local";
    unsigned port = 27020, seconds = 30;
    bool pulse = false, fire = false, afterburner = false;
    auto aircraft = ofs::AircraftType::A320;
    for (int i = 1; i < argc; ++i) {
      std::string_view a = argv[i];
      if (a == "--server")
        address = argument(i, argc, argv);
      else if (a == "--aircraft")
        aircraft = ofs::aircraftTypeFromName(argument(i, argc, argv));
      else if (a == "--port")
        port = number(argument(i, argc, argv), 1, 65535);
      else if (a == "--name")
        name = argument(i, argc, argv);
      else if (a == "--seconds")
        seconds = number(argument(i, argc, argv), 1, 86400);
      else if (a == "--conditions")
        preset = argument(i, argc, argv);
      else if (a == "--fire")
        fire = true;
      else if (a == "--pulse")
        pulse = true;
      else if (a == "--afterburner")
        afterburner = true;
      else
        throw std::invalid_argument(
            "ofs_bot [--server IP] [--port N] [--name ASCII] [--seconds N] "
            "[--aircraft a320|su57|typhoon|sr71|jf17|b52] [--conditions local|good|moderate|bad] [--pulse] [--fire] [--afterburner]");
    }
    Client client(name, aircraft);
    Transport::conditions(preset);
    client.connect(address, static_cast<std::uint16_t>(port));
    ofs::FixedStepClock clock;
    auto begin = std::chrono::steady_clock::now(), last = begin, log = begin;
    bool connected = false;
    std::uint64_t scripted = 0;
    while (std::chrono::steady_clock::now() - begin <
           std::chrono::seconds(seconds)) {
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      client.poll(dt);
      if (client.ready()) {
        connected = true;
        client.setFiring(fire);
        clock.advance(dt, [&](double) {
          auto c = client.prediction().simulator().controls();
          if (afterburner && ofs::aircraftDefinition(aircraft).flight.afterburner_thrust_each>0)
            c.throttle[0]=c.throttle[1]=1;
          c.aileron_stick = pulse && scripted < 120 ? .08 : 0;
          client.predict(c);
          ++scripted;
        });
        if (!finiteState(client.prediction().simulator().state()))
          throw std::runtime_error("nonfinite bot state");
      }
      if (now - log >= std::chrono::seconds(5)) {
        const auto &s = client.stats();
        const auto &p = client.prediction().stats();
        std::printf(
            "[BOT] state=%s id=%llu tick=%llu remote=%zu ping=%d "
            "snapshots=%.1f inputs=%.1f pending=%zu corrections=%llu "
            "error=%.4f peakError=%.4f bytes=%llu/%llu wireBps=%.0f/%.0f "
            "history=%zu\n",
            client.status().c_str(), (unsigned long long)client.entity(),
            (unsigned long long)s.serverTick, client.remotes().size(), s.pingMs,
            s.snapshotHz, s.inputHz, client.prediction().pending().size(),
            (unsigned long long)p.reconciliations, p.error, p.maxError,
            (unsigned long long)s.bytesIn, (unsigned long long)s.bytesOut,
            s.wireIn, s.wireOut, s.historySamples);
        std::printf("[COMBAT] health=%.0f ammo=%u life=%u shots=%llu "
                    "hitsReceived=%llu kills=%u deaths=%u visual=%zu\n",
                    client.life().health, client.life().ammo,
                    client.life().generation, (unsigned long long)s.shots,
                    (unsigned long long)s.hitsReceived, client.life().kills,
                    client.life().deaths, client.visualCount());
        std::fflush(stdout);
        log = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!connected || !client.ready() || !client.stats().snapshots)
      throw std::runtime_error(
          "bot did not remain connected and receive snapshots");
    client.disconnect();
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[BOT] %s\n", e.what());
    return 1;
  }
}
