"""Version/update and installed-file manifests. HTTPS is the trust anchor in v1."""
from dataclasses import dataclass
import platform
import re
from urllib.parse import urlsplit
from .storage import LauncherError, relative_name
from .version import Version

MAX_PACKAGE = 8 * 1024**3
MAX_EXPANDED = 16 * 1024**3
MAX_FILES = 30000


def https_url(value):
    if not isinstance(value, str) or len(value) > 4096 or any(c.isspace() for c in value):
        raise LauncherError('Invalid HTTPS URL')
    try:
        url = urlsplit(value)
        if url.scheme != 'https' or not url.hostname or url.username or url.password or url.fragment or url.port not in (None, 443):
            raise ValueError()
    except ValueError as exc:
        raise LauncherError('Updates require HTTPS on port 443 without embedded credentials') from exc
    return value


def platform_id():
    arch = platform.machine().lower()
    if arch not in ('x86_64', 'amd64'):
        raise LauncherError('Release packages currently support x86-64')
    system = platform.system().lower()
    if system not in ('windows', 'linux'):
        raise LauncherError('Supported platforms are Windows and Linux')
    return f'{system}-x86_64'


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise LauncherError(f'Invalid {label}')
    return value


def digest(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise LauncherError('Expected lower-case SHA-256 digest')
    return value


def file_table(data, urls=False):
    if not isinstance(data, dict) or not 1 <= len(data) <= MAX_FILES:
        raise LauncherError('Invalid release file list')
    seen = set()
    total = 0
    for name, record in data.items():
        relative_name(name)
        if name.casefold() in seen or name.casefold() == 'release.json':
            raise LauncherError('Duplicate/reserved file path')
        seen.add(name.casefold())
        if not isinstance(record, dict):
            raise LauncherError('Invalid file record')
        total += integer(record.get('size'), 0, MAX_PACKAGE, 'file size')
        digest(record.get('sha256'))
        if type(record.get('mode')) is not int or record.get('mode') not in (0o644, 0o755):
            raise LauncherError('Invalid file permissions')
        if urls:
            https_url(record.get('url'))
    if total > MAX_EXPANDED:
        raise LauncherError('Release exceeds expanded size limit')
    for name in seen:
        if any('/'.join(name.split('/')[:n]) in seen for n in range(1, len(name.split('/')))):
            raise LauncherError('File/directory path collision')
    return data


def release_metadata(data):
    if not isinstance(data, dict) or type(data.get('schema')) is not int or data.get('schema') != 1:
        raise LauncherError('Unsupported release manifest schema')
    Version(data.get('version'))
    if data.get('channel') not in ('stable', 'development') or data.get('platform') not in ('linux-x86_64', 'windows-x86_64'):
        raise LauncherError('Invalid release platform/channel')
    file_table(data.get('files'))
    suffix = '.exe' if data['platform'].startswith('windows') else ''
    for required in ('ofs_client' + suffix, 'launcher/ofs_launcher' + suffix, 'launcher-catalog.json', 'build-info.json'):
        if required not in data['files']:
            raise LauncherError(f'Release is missing {required}')
    return data


@dataclass(frozen=True)
class Release:
    data: dict

    @property
    def version(self):
        return self.data['version']

    @property
    def package(self):
        return self.data['package']

    @property
    def metadata(self):
        return {k: self.data[k] for k in ('schema', 'version', 'channel', 'platform', 'files')}

    @classmethod
    def parse(cls, data):
        release_metadata(data)
        Version(data.get('minimum_launcher_version'))
        integer(data.get('minimum_bootstrap_protocol'), 1, 1, 'bootstrap protocol')
        for package in (data.get('package'), *(data[key] for key in ('installer', 'setup') if key in data)):
            if not isinstance(package, dict):
                raise LauncherError('Missing package record')
            https_url(package.get('url'))
            digest(package.get('sha256'))
            integer(package.get('size'), 1, MAX_PACKAGE, 'package size')
        file_table(data['files'], urls=True)
        if not isinstance(data.get('notes', ''), str) or len(data.get('notes', '')) > 50000:
            raise LauncherError('Invalid release notes')
        return cls(data)


class NoReleaseError(LauncherError):
    """The feed is valid but publishes nothing for this channel and platform."""


# Launchers before 0.6.9 read versions of exactly three numbers, and turn down
# the whole index over one entry they cannot read. Versions of four numbers
# (0.7.0.1) are therefore listed apart, under EXTENDED, which those launchers
# never look at: they update to the next version of three numbers, whose
# launcher reads both lists. Each list holds at most 100 releases.
EXTENDED = 'extended_releases'


def index_list(version):
    """The list of the update index a version belongs in."""
    return EXTENDED if len(Version(version).core) > 3 else 'releases'


def index_entries(data):
    """Every release descriptor of an update index, from both of its lists."""
    if not isinstance(data, dict) or type(data.get('schema')) is not int or data.get('schema') != 1:
        raise LauncherError('Unsupported update manifest')
    lists = (data.get('releases'), data.get(EXTENDED, []))
    if not all(isinstance(entries, list) and len(entries) <= 100 for entries in lists):
        raise LauncherError('Unsupported update manifest')
    return [*lists[0], *lists[1]]


def select_release(data, channel, target=None):
    entries = index_entries(data)
    target = target or platform_id()
    releases = [Release.parse(entry) for entry in entries]
    matches = [r for r in releases if r.data['channel'] == channel and r.data['platform'] == target]
    if not matches:
        raise NoReleaseError('No release is published for this channel/platform')
    if len({r.version for r in matches}) != len(matches):
        raise LauncherError('Ambiguous release versions')
    return max(matches, key=lambda r: Version(r.version))
