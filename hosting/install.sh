#!/bin/sh
# Sets up the OpenFlightSim public game host on a Debian or Ubuntu computer.
# Run as root from this folder. Safe to run again after updating these files.
set -eu
cd "$(dirname "$0")"
apt-get install -y --no-install-recommends git cmake ninja-build g++ python3 libssl-dev libprotobuf-dev protobuf-compiler openssh-client ca-certificates
id ofs-public >/dev/null 2>&1 || useradd --system --home-dir /var/lib/ofs-public --shell /usr/sbin/nologin ofs-public
install -d -o root -g root -m 755 /opt/ofs-public
install -o root -g root -m 755 public_host.py build_server.py publish_address.py /opt/ofs-public/
install -d -o ofs-public -g ofs-public -m 755 /var/lib/ofs-public /var/lib/ofs-public/builds /var/lib/ofs-public/work /var/lib/ofs-public/host
install -d -o ofs-public -g ofs-public -m 700 /var/lib/ofs-public/address
if [ ! -f /var/lib/ofs-public/address/deploy_key ]; then
  runuser -u ofs-public -- ssh-keygen -q -t ed25519 -N '' -C 'OpenFlightSim public server address' -f /var/lib/ofs-public/address/deploy_key
fi
install -o root -g root -m 644 systemd/ofs-public-*.service systemd/ofs-public-*.timer /etc/systemd/system/
systemctl daemon-reload
# The one port the router forwards here.
if command -v ufw >/dev/null 2>&1; then ufw allow 27020/udp comment OpenFlightSim >/dev/null; fi
systemctl enable --now ofs-public-host.service ofs-public-build.timer ofs-public-address.timer
systemctl restart ofs-public-host.service
echo 'Deploy key for the address repository (add it with write access):'
cat /var/lib/ofs-public/address/deploy_key.pub
