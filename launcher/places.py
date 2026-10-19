"""Where on the Earth map a free flight starts: a latitude and longitude, or the
name of a town, city or airport from data/launcher/places.json (built by
scripts/build_place_list.py)."""

import json
import re
import unicodedata
from pathlib import Path

from .storage import LauncherError

_NUMBER = r'([+-]?\d+(?:\.\d+)?)\s*°?\s*([NSEWnsew]?)'
_COORDINATES = re.compile(r'^\s*' + _NUMBER + r'\s*[,; ]\s*' + _NUMBER + r'\s*$')


def _fold(text):
    """Lower-case ASCII, without accents or punctuation, single spaces."""
    text = unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode('ascii').lower()
    return ' '.join(re.sub(r'[^a-z0-9]+', ' ', text).split())


def coordinates(text):
    """Latitude and longitude in degrees from '55.75, 37.6', '55.75 37.6' or
    '55.75N 37.6E', or None when the text is not a pair of numbers."""
    match = _COORDINATES.match(text)
    if not match:
        return None
    values = []
    for number, hemisphere in (match.group(1, 2), match.group(3, 4)):
        value = float(number)
        if hemisphere.upper() in ('S', 'W'):
            value = -abs(value)
        values.append((value, hemisphere.upper()))
    (lat, a), (lon, b) = values
    if a in ('E', 'W') or b in ('N', 'S'):
        if (a in ('E', 'W')) != (b in ('N', 'S')):
            raise LauncherError('Give the latitude (N/S) and the longitude (E/W) once each')
        lat, lon = lon, lat
    if not -90 <= lat <= 90 or not -180 <= lon <= 180:
        raise LauncherError('Latitude must be -90 to 90 and longitude -180 to 180')
    return lat, lon


class Places:
    """The names in a places file."""

    def __init__(self, path):
        try:
            data = json.loads(Path(path).read_text(encoding='ascii'))
        except (OSError, ValueError) as exc:
            raise LauncherError('The list of places is missing or damaged; type a latitude and longitude instead') from exc
        if not isinstance(data, dict) or data.get('schema') != 1 or not isinstance(data.get('places'), list) \
                or not isinstance(data.get('airports'), list):
            raise LauncherError('Invalid list of places')
        self.places = data['places']
        self.airports = data['airports']

    def find(self, text):
        """(latitude, longitude, description) of the best match for a name:
        a place's name (the most populous one of that name) or 'name,
        country', an airport's ICAO or IATA code or name, or else the start of
        a name."""
        query = _fold(text)
        if not query:
            raise LauncherError('Type a place, an airport or a latitude and longitude')
        before, _, after = text.partition(',')
        name, country = _fold(before), _fold(after)
        exact = [p for p in self.places if _fold(p[0]) == name and (not country or _fold(p[1]).startswith(country))]
        if exact:
            place, nation, lat, lon, _ = exact[0]  # the file lists the most populous first
            return lat, lon, f'{place}, {nation}'
        if len(query) in (3, 4) and query.isalnum():
            for ident, iata, airport, city, nation, lat, lon in self.airports:
                if query.upper() in (ident, iata):
                    return lat, lon, f'{airport} ({ident}), {city or nation}'
        for ident, iata, airport, city, nation, lat, lon in self.airports:
            folded = _fold(airport)
            if folded == query or folded == query + ' airport' or folded == query + ' international airport':
                return lat, lon, f'{airport} ({ident}), {city or nation}'
        starts = [p for p in self.places if _fold(p[0]).startswith(name) and (not country or _fold(p[1]).startswith(country))]
        if starts:
            place, nation, lat, lon, _ = starts[0]
            return lat, lon, f'{place}, {nation}'
        for ident, iata, airport, city, nation, lat, lon in self.airports:
            if query in _fold(airport) or (city and _fold(city) == query):
                return lat, lon, f'{airport} ({ident}), {city or nation}'
        raise LauncherError(f'No place called "{text.strip()}" is known; try a nearby city, or type a latitude and longitude')


def resolve(text, path):
    """(latitude, longitude, description) of what a pilot typed: coordinates,
    or a name looked up in the places file at `path`."""
    if len(text) > 120:
        raise LauncherError('The place is too long')
    found = coordinates(text)
    if found:
        return found[0], found[1], f'{found[0]:.4f}, {found[1]:.4f}'
    return Places(path).find(text)
