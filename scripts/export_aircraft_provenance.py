#!/usr/bin/env python3
"""Export all scalar/vector flight configuration fields to auditable SI manifests.
No external-source claim is inferred from a code comment. Published data without
a retrieved, parameter-specific primary source remains ESTIMATE here.
"""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

MASS={'mass','empty_mass','initial_fuel','initial_payload','fuel_capacity'}
LENGTH={'wing_span','mac','pitch_arm','pitch_span','oleo_stroke'}
ANGLE={'alpha0','alpha_crit_clean','alpha_limit','elev_max','elev_min','ail_max','rud_max','levcon_max','input_deadzone'}-{'input_deadzone'}
INERTIA={'ixx','iyy','izz','ixy','ixz','iyz'}
TIME={'engine_tau','response_time','input_tau'}
RATE={'actuator_rate','flap_rate','spoiler_rate','inlet_spike_rate'}

def export(build,binary=None,emit_cpp=None):
    header=(ROOT/'core/include/ofs/aircraft.hpp').read_text()
    body=header.split('struct AircraftConfig {',1)[1].split('\n};',1)[0]
    body=re.sub(r'//[^\n]*','',body)
    doubles=[]; vectors=[]
    for kind,names in re.findall(r'\b(double|Vec3)\s+([^;]+);',body):
        names=re.sub(r'\{[^{}]*\}','',names)
        for item in names.split(','):
            match=re.match(r'\s*(\w+)(?:\[(\d+)\])?',item)
            if match:
                name,count=match.groups()
                for k in range(int(count or 1)):
                    (doubles if kind=='double' else vectors).append(name+('['+str(k)+']' if count else ''))
    statements=[]
    for name in doubles:
        factor='*ofs::kDeg2Rad' if name=='flap_max_deg' else ''
        statements.append('emit("'+name+'",c.'+name+factor+');')
    for name in vectors:
        statements.append('emit("'+name+'",c.'+name+');')
    source='''#include "ofs/aircraft_definition.hpp"
#include "ofs/simulator.hpp"
#include <iostream>
#include <iomanip>
void emit(const char* n,double v){std::cout<<n<<"="<<std::setprecision(17)<<v<<"\\n";}
void emit(const char* n,ofs::Vec3 v){std::cout<<n<<"="<<std::setprecision(17)<<v.x<<","<<v.y<<","<<v.z<<"\\n";}
int main(){for(const auto& d:ofs::aircraftDefinitions()){std::cout<<"AIRCRAFT="<<d.key<<"\\n";ofs::Simulator configured(d.flight);const auto& c=configured.config();
'''+ '\n'.join(statements)+'''
for(std::size_t k=0;k<c.engines.size();++k){const auto& e=c.engines[k];const auto prefix="engines["+std::to_string(k)+"].";
'''
    for field in ['position','direction','dry_thrust','reheat_thrust','spool_seconds','dry_tsfc','reheat_tsfc','vector_axis','vector_limit','vector_rate','nozzle_pivot']:
        source+='emit((prefix+"'+field+'").c_str(),e.'+field+');\n'
    source+='}\nfor(std::size_t k=0;k<c.surfaces.size();++k){const auto prefix="surfaces["+std::to_string(k)+"].";emit((prefix+"position").c_str(),c.surfaces[k].position);emit((prefix+"area_fraction").c_str(),c.surfaces[k].area_fraction);}\n'
    source+='emit("engine_count",c.engine_count);emit("control_law",double(c.control_law));emit("variable_inlets",c.variable_inlets);emit("contacts_enabled",c.contacts_enabled);\n'
    source+='for(std::size_t k=0;k<c.belly_contacts.size();++k)emit(("belly_contacts["+std::to_string(k)+"]").c_str(),c.belly_contacts[k]);\n}}'
    if emit_cpp:
        emit_cpp.write_text(source);return {}
    with tempfile.TemporaryDirectory() as td:
        cpp=Path(td)/'export.cpp';exe=Path(td)/'export';cpp.write_text(source)
        if binary is None: subprocess.run(['c++','-std=c++23','-I'+str(ROOT/'core/include'),str(cpp),str(build/'core/libofs_core.a'),'-o',str(exe)],check=True)
        lines=subprocess.check_output([str(binary or exe)],text=True).splitlines()
    datasets={};current=None
    source_files=[ROOT/'core/src'/name for name in ['aircraft.cpp','aircraft_definition.cpp','sr71.cpp','su57.cpp']]+[ROOT/'core/include/ofs/aircraft.hpp']+list((ROOT/'data/physics').glob('*.json'))+[ROOT/'data/reference/parameter_sources.json']
    reviewed=json.loads((ROOT/'data/reference/parameter_sources.json').read_text())
    for name in ['a320','su57']:
        definition=json.loads((ROOT/'data/physics'/f'{name}.json').read_text())
        reviewed[name]={r['parameter']:r for r in definition['parameters']}
        reviewed[name]['_configuration']=definition['configuration']
    revision=hashlib.sha256(b''.join(p.read_bytes() for p in sorted(source_files))).hexdigest()
    for line in lines:
        key,text=line.split('=',1)
        if key=='AIRCRAFT':
            current=dict(schema_version=1,aircraft=text,configuration='Registry initial load, engineering surrogate',
                         model_provenance='data/models/engineering-fallback.json',parameters=[])
            datasets[text]=current;continue
        values=[float(v) for v in text.split(',')]
        unit='1'
        if key in MASS:unit='kg'
        elif key in {'fuel_inertia_per_kg','payload_inertia_per_kg','wing_area'}:unit='m2'
        elif key in LENGTH or key.endswith(('.position','.nozzle_pivot')) or key in vectors or key.startswith('belly_contacts'):unit='m'
        elif key in ANGLE or key=='flap_max_deg' or key.endswith('.vector_limit'):unit='rad'
        elif key in INERTIA:unit='kg m2'
        elif key in TIME or key.endswith('.spool_seconds'):unit='s'
        elif key in RATE:unit='1/s'
        elif key in {'max_pitch_rate','max_roll_rate'} or key.endswith('.vector_rate'):unit='rad/s'
        elif key in {'thrust_sl_static_each','afterburner_thrust_each'} or key.endswith(('.dry_thrust','.reheat_thrust')):unit='N'
        elif key=='oleo_k':unit='N/m'
        elif key=='oleo_c':unit='N s/m'
        elif key=='control_q_limit':unit='Pa'
        elif key.endswith(('dry_tsfc','reheat_tsfc')):unit='kg/(N s)'
        elif key=='fuel_inertia_per_kg':unit='m2'
        elif key=='payload_cd_per_kg':unit='1/kg'
        elif key in {'cl_alpha','cm_alpha','cm_de','cl_beta','cl_da','cn_beta','cn_dr','cy_beta'}:unit='1/rad'
        origin='ESTIMATE'
        if key in INERTIA and current['aircraft'] in {'a320','su57','typhoon'}:origin='DERIVED'
        notes='Engineering value; no independent flight-test validation. Published-looking anchors remain estimates until source verified.'
        if origin=='DERIVED':notes='Derived from estimated component masses, locations and variances; not measured aircraft inertia.'
        if key in {'ixy','iyz'} and values==[0.]:notes='Assumed symmetry; unavailable measured products, not a measurement of zero.'
        record=dict(parameter=key,values=values[0] if len(values)==1 else values,unit=unit,
            provenance=origin,source='repository configuration '+current['aircraft'],revision=revision,
            configuration=current['configuration'],validity_envelope='Only regression/plausibility scenarios; no validated flight envelope',
            confidence='engineering estimate',validation_status='NOT_EXTERNALLY_VALIDATED',notes=notes)
        records=reviewed.get(current['aircraft'],{})
        if key in records:
            authored=records[key]
            expected=authored.get('values')*(3.141592653589793/180) if key=='flap_max_deg' else authored.get('values')
            if authored.get('field') and record['values']!=expected:
                raise ValueError('Runtime differs from reviewed authoring source: '+current['aircraft']+' '+key)
            record.update({k:authored[k] for k in ['provenance','source','notes','unit']})
            if key=='flap_max_deg':record['unit']='rad'
            record['configuration']=records.get('_configuration',record['configuration'])
            record['confidence']='Reference source anchor' if record['provenance']=='REFERENCE' else 'Derived from estimates' if record['provenance']=='DERIVED' else 'Engineering estimate; see source'
        if current['aircraft']=='typhoon' and key in INERTIA:record['source']='core/src/aircraft_definition.cpp component mass/geometry reconstruction; docs/MASS_ENGINE_AUDIT.md'
        if key.endswith(('.direction','.vector_axis')):record['unit']='1'
        if key.startswith('unsteady_') and key.endswith('tau'):record['unit']='s'
        current['parameters'].append(record)
    return datasets

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,default=ROOT/'build/validation');parser.add_argument('--binary',type=Path);parser.add_argument('--emit-cpp',type=Path);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    data=export(args.build.resolve(),args.binary,args.emit_cpp)
    if args.check:
        shared=json.loads((ROOT/'data/models/engineering-fallback.json').read_text())
        for record in shared['parameters']:
            path=ROOT/record['source'].split('::',1)[0]
            if hashlib.sha256(path.read_bytes()).hexdigest()!=record['revision']:
                raise SystemExit('Shared model provenance stale: '+record['parameter'])
    for name,dataset in data.items():
        path=ROOT/'data/aircraft'/f'{name}.json';text=json.dumps(dataset,indent=2,allow_nan=False)+'\n'
        if args.check:
            if path.read_text()!=text:raise SystemExit('Provenance stale: '+str(path))
        else:path.write_text(text)
        print(name,len(dataset['parameters']),'auditable parameters')
