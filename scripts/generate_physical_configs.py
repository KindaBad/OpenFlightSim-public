#!/usr/bin/env python3
"""Compile reviewed SI aircraft definitions. JSON is the authoring source.
Generated C++ stays in the build tree. Blender consumes these same JSON files.
"""
import argparse
import json
import math
import re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ORIGINS = {'REFERENCE', 'DERIVED', 'ESTIMATE', 'CALIBRATED_APPROXIMATION'}
def literal(value):
    if isinstance(value, list):
        return '{' + ','.join(literal(v) for v in value) + '}'
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Expected finite numerical SI value')
    return repr(value)
def read(name):
    data = json.loads((ROOT/'data/physics'/f'{name}.json').read_text())
    for record in data['parameters']:
        assert record['provenance'] in ORIGINS
        assert record['source'] and record['notes'] and record['unit']
        literal(record['values'])
    return data

def emit(path):
    lines = ['// Generated from data/physics; edit the JSON authoring sources.',
             '#include "ofs/aircraft.hpp"', '#include "ofs/physical_geometry.hpp"', '#include "ofs/airliner.hpp"', 'namespace ofs {']
    for name in ['a320', 'su57']:
        data = read(name)
        params = {r['parameter']: r['values'] for r in data['parameters']}
        def v(key): return literal(params[key])
        hinges = data['hinges']
        lines.append(f'static const std::array<GeometryHinge,{len(hinges)}> {name}Hinges{{{{')
        for h in hinges:
            lines.append('{'+json.dumps(h['name'])+','+json.dumps(h['channel'])+','+v(h['parameter'])+'},')
        lines.append('}};')
        lines.append(f'const PhysicalGeometry& {name}Geometry() {{')
        lines.append('static const PhysicalGeometry g{'+','.join(v(k) for k in
           ['length','wing_span','height','wing_area','mac','wing_sweep','asset_cg','aerodynamic_reference'])+',')
        lines.append('{{'+v('engine_pos_l')+','+v('engine_pos_r')+'}},')
        lines.append('{{'+v('nozzle_pivot_l')+','+v('nozzle_pivot_r')+'}},')
        lines.append('{{'+','.join(v(k) for k in ['gear_nose','gear_main_l','gear_main_r'])+'}},'+name+'Hinges,{{'+','.join(literal(site) for site in data['force_sites'])+'}}}; return g; }')
        lines.append(f'const ComponentMassDistribution& {name}MassDistribution() {{ static const ComponentMassDistribution d{{'+','.join(v(k) for k in ['component_engine_mass','structure_variance','engine_variance','fuel_variance','payload_variance'])+'};return d;}')
        if name == 'a320':
            lines.append('const std::array<HighLiftConfiguration,6>& a320HighLiftTable() { static const std::array<HighLiftConfiguration,6> table{{')
            for row in data['high_lift_schedule']:
                row = list(row);row[4] *= math.pi/180
                lines.append(literal(row)+',')
            lines.append('}};return table;}')
        lines.append(f'void apply{name.upper()}PhysicalConfig(AircraftConfig& c) {{')
        for r in data['parameters']:
            field = r.get('field')
            if field:
                if not re.fullmatch(r'[a-zA-Z_][a-zA-Z_0-9]*(?:\[\d+\])?(?:\.[a-zA-Z_][a-zA-Z_0-9]*)?',field):
                    raise ValueError('Invalid C++ member')
                lines.append(f'c.{field}={literal(r["values"])};')
        lines.append('}')
    lines.append('}')
    path.write_text('\n'.join(lines)+'\n')
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    emit(parser.parse_args().output)
