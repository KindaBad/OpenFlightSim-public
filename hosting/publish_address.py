#!/usr/bin/env python3
"""Tell launchers where the public host is. Standard library only.

A home connection's address changes now and then. This finds the address the
internet sees this computer at and, when it has changed, writes it to
`public.json` in a small public repository that launchers read over HTTPS
(launcher/public.py). The repository is pushed to with a deploy key that can
write to nothing else.
"""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

SERVICES = ('https://api.ipify.org', 'https://checkip.amazonaws.com', 'https://ipv4.icanhazip.com')
# GitHub's published host key, so the push cannot be answered by anyone else.
GITHUB_KEY = 'github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl\n'


def public_address():
    """The address two independent services agree this computer has, or None."""
    seen = []
    for service in SERVICES:
        try:
            with urllib.request.urlopen(service, timeout=10) as response:
                address = ipaddress.IPv4Address(response.read(64).decode('ascii').strip())
        except (OSError, ValueError):
            continue
        if not address.is_global:
            continue
        if str(address) in seen:
            return str(address)
        seen.append(str(address))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', required=True, help='folder holding the key and the checkout')
    parser.add_argument('--repository', default='git@github.com:KindaBad/OpenFlightSim-servers.git')
    parser.add_argument('--port', type=int, default=27020)
    options = parser.parse_args()
    root = Path(options.root).resolve()
    address = public_address()
    if not address:
        sys.exit('Could not find this computer\'s public address')
    wanted = json.dumps(dict(schema=1, address=address, port=options.port), indent=2) + '\n'
    checkout = root / 'directory'
    if (checkout / 'public.json').is_file() and (checkout / 'public.json').read_text() == wanted and (root / 'published').is_file():
        return
    hosts = root / 'known_hosts'
    hosts.write_text(GITHUB_KEY)
    environment = {**os.environ, 'GIT_SSH_COMMAND': f'ssh -i {root / "deploy_key"} -o IdentitiesOnly=yes -o BatchMode=yes '
                                                      f'-o UserKnownHostsFile={hosts} -o StrictHostKeyChecking=yes'}

    def git(*arguments, cwd=checkout):
        subprocess.run(('git', *arguments), cwd=cwd, check=True, env=environment, stdin=subprocess.DEVNULL)
    if not (checkout / '.git').is_dir():
        git('clone', '--quiet', options.repository, str(checkout), cwd=root)
    else:
        git('fetch', '--quiet', 'origin', 'main')
        git('reset', '--quiet', '--hard', 'origin/main')
    (root / 'published').unlink(missing_ok=True)
    if (checkout / 'public.json').read_text() != wanted:
        (checkout / 'public.json').write_text(wanted)
        git('-c', 'user.name=OpenFlightSim public server', '-c', 'user.email=noreply@github.com', 'commit', '--quiet',
            '-m', 'Public server address', 'public.json')
        git('push', '--quiet', 'origin', 'HEAD:main')
        print(f'Published {address}:{options.port}', flush=True)
    (root / 'published').write_text(address)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(f'git {exc.cmd[1]} failed with {exc.returncode}')
