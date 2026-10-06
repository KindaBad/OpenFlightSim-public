"""Strict SemVer precedence; build metadata never changes precedence."""
from functools import total_ordering
import re
from .storage import LauncherError


@total_ordering
class Version:
    def __init__(self, value):
        if not isinstance(value, str) or len(value) > 128:
            raise LauncherError('Version must be a string of at most 128 characters')
        match = re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?', str(value))
        if not match:
            raise LauncherError(f'Invalid semantic version: {value}')
        self.core = tuple(int(n) for n in match.group(1, 2, 3))
        self.pre = tuple(match[4].split('.')) if match[4] else ()
        for identifier in (*self.pre, *(match[5].split('.') if match[5] else ())):
            if not identifier:
                raise LauncherError('Empty version identifier')
        if any(i.isdigit() and len(i) > 1 and i[0] == '0' for i in self.pre):
            raise LauncherError('Leading zero in prerelease')

    def __eq__(self, other):
        if not isinstance(other, Version):
            return NotImplemented
        return (self.core, self.pre) == (other.core, other.pre)

    def __lt__(self, other):
        if self.core != other.core:
            return self.core < other.core
        if not self.pre or not other.pre:
            return bool(self.pre) and not other.pre
        for a, b in zip(self.pre, other.pre):
            if a == b:
                continue
            if a.isdigit() and b.isdigit():
                return int(a) < int(b)
            if a.isdigit() != b.isdigit():
                return a.isdigit()
            return a < b
        return len(self.pre) < len(other.pre)
