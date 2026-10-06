"""Stable bootstrap/updater. Has no Qt dependency and never replaces itself."""
import argparse
import logging
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
from .diagnostics import configure_logs
from .installation import Lease, active_directory, activate, extract_package, prune, recover, rollback, verify
from .manifest import Release
from .platform_process import spawn
from .shortcuts import install as install_shortcut
from .storage import LauncherError, read_json, safe_path


def run(root, action=None, request=None, restart=True, user_data=None):
    root = Path(root).resolve()
    configure_logs(Path(user_data) / 'logs' if user_data else None, name='updater')
    log = logging.getLogger('ofs.bootstrap')
    lease = Lease(root).acquire(wait=60 if action else 0)
    try:
        recover(root)
        if action == 'apply':
            data = read_json(request)
            release = Release.parse(data['release'])
            build = read_json(active_directory(root) / 'build-info.json')
            from .version import Version
            if Version(build['version']) < Version(release.data['minimum_launcher_version']):
                raise LauncherError('This update requires a newer launcher/bootstrap distribution')
            if data['kind'] == 'update':
                package = safe_path(root, data['package'])
                if not str(data['package']).startswith('.downloads/'):
                    raise LauncherError('Package must be in installation download cache')
                stage = safe_path(root, '.staging/' + uuid.uuid4().hex)
                extract_package(package, stage, release)
            elif data['kind'] == 'repair':
                stage = safe_path(root, data['stage'])
                if not data['stage'].startswith('.staging/'):
                    raise LauncherError('Repair must be staged')
                # Verify staged files against the HTTPS release descriptor, not just
                # the locally written release.json, before switching the pointer.
                if verify(stage, release.metadata):
                    raise LauncherError('Repair stage differs from published release')
                local = read_json(stage / 'release.json')
                expected = {**release.metadata, 'files': {k: {f: v for f, v in r.items() if f != 'url'} for k, r in release.data['files'].items()}}
                if local != expected:
                    raise LauncherError('Repair manifest differs from published release')
            else:
                raise LauncherError('Invalid update request')
            activate(root, stage, f'releases/{release.version}-{uuid.uuid4().hex[:12]}')
            Path(request).unlink(missing_ok=True)
            # Only the new active and one rollback slot are retained.
            prune(root)
        elif action == 'rollback':
            rollback(root)
        directory = active_directory(root)
        executable = safe_path(directory, 'launcher/ofs_launcher' + ('.exe' if os.name == 'nt' else ''))
        if not executable.is_file():
            raise LauncherError('Launcher is missing. Restore a distribution package or use updater rollback.')
        if restart and getattr(sys, 'frozen', False):
            # Players start from the application menu after the first run. The entry
            # is refreshed here because the icon lives in the replaceable release slot.
            install_shortcut(sys.executable, icon=directory / 'data/launcher/icon.svg', state=user_data)
    finally:
        lease.close()
    if restart:
        # The new launcher takes the lease immediately; no shell command involved.
        log.info('Starting launcher from %s', directory)
        command = [str(executable), '--installation', str(root)]
        if user_data:
            command += ['--user-data', str(user_data)]
        spawn(command, cwd=root)


def main():
    parser = argparse.ArgumentParser(description='OpenFlightSim bootstrap and verified update helper')
    parser.add_argument('--installation', type=Path)
    parser.add_argument('--apply', type=Path)
    parser.add_argument('--rollback', action='store_true')
    parser.add_argument('--no-restart', action='store_true')
    parser.add_argument('--user-data', type=Path)
    args = parser.parse_args()
    root = args.installation or (Path(sys.executable).parent if getattr(sys, 'frozen', False) else Path.cwd())
    try:
        run(root, 'apply' if args.apply else 'rollback' if args.rollback else None, args.apply, not args.no_restart, args.user_data)
        return 0
    except Exception as exc:
        logging.getLogger('ofs.bootstrap').error('Updater failed: %s', exc)
        # Persist a concise result for the launcher to present on the next start.
        from .storage import write_json
        try:
            write_json(root / 'last-update-result.json', {'ok': False, 'message': str(exc)})
        except OSError:
            pass
        if (args.apply or args.rollback) and not args.no_restart:
            try:
                run(root, restart=True, user_data=args.user_data)
            except Exception as restart_error:
                logging.getLogger('ofs.bootstrap').error('Could not restart previous launcher: %s', restart_error)
        if not getattr(sys, 'frozen', False) or sys.stderr:
            print(f'OpenFlightSim updater: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
