#!/usr/bin/env python3
"""Builds data/launcher/places.json, the names the launcher can start an Earth
flight at: Natural Earth's populated places and OurAirports' large and medium
airports (both public domain; see licenses/data/EARTH.md). Run after the Earth
downloads (scripts/build_earth_terrain.py) have fetched them into build/earth/dl."""

import argparse
import csv
import json
import struct
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DL = ROOT / 'build' / 'earth' / 'dl'


def dbf_records(path):
    """The records of a dBase III table, as dicts of stripped strings."""
    data = Path(path).read_bytes()
    count, header, size = struct.unpack_from('<IHH', data, 4)
    fields, offset = [], 32
    while data[offset] != 0x0D:
        name = data[offset:offset + 11].split(b'\0')[0].decode('ascii')
        fields.append((name, data[offset + 16]))
        offset += 32
    for i in range(count):
        record = data[header + i * size:header + (i + 1) * size]
        if record[:1] == b'*':
            continue
        values, at = {}, 1
        for name, length in fields:
            values[name] = record[at:at + length].decode('utf-8', 'replace').strip()
            at += length
        yield values


def ascii_name(text):
    return unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode('ascii').strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--places', type=Path, default=DL / 'ne' / 'ne_10m_populated_places_simple' / 'ne_10m_populated_places_simple.dbf')
    p.add_argument('--airports', type=Path, default=DL / 'oa' / 'airports.csv')
    p.add_argument('--out', type=Path, default=ROOT / 'data' / 'launcher' / 'places.json')
    args = p.parse_args()
    countries = {}
    places = []
    for r in dbf_records(args.places):
        name = ascii_name(r.get('nameascii') or r['name'])
        if not name:
            continue
        country = ascii_name(r.get('adm0name', ''))
        countries[r.get('iso_a2', '')] = country
        places.append([name, country, round(float(r['latitude']), 4), round(float(r['longitude']), 4),
                       int(float(r.get('pop_max') or 0))])
    airports = []
    for r in csv.DictReader(open(args.airports, encoding='utf-8')):
        if r['type'] not in ('large_airport', 'medium_airport'):
            continue
        airports.append([r['ident'], r.get('iata_code', ''), ascii_name(r['name']), ascii_name(r.get('municipality', '')),
                         countries.get(r.get('iso_country', ''), r.get('iso_country', '')),
                         round(float(r['latitude_deg']), 4), round(float(r['longitude_deg']), 4)])
    places.sort(key=lambda p: -p[4])
    airports.sort()
    out = {'schema': 1, 'places': places, 'airports': airports}
    args.out.write_text(json.dumps(out, separators=(',', ':'), ensure_ascii=True) + '\n', encoding='ascii')
    print(f'{len(places)} places, {len(airports)} airports -> {args.out} ({args.out.stat().st_size / 1024:.0f} KiB)')


if __name__ == '__main__':
    main()
