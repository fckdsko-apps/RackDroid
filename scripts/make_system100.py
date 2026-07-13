#!/usr/bin/env python3
"""Build a Roland System-100-style patch (.vcv = zstd(tar(patch.json))).
Architecture = System 100 Model 101 voice + Model 104 sequencer:
  SEQ3 -> VCO -> VCF -> VCA -> Audio, ADSR gates the VCA AND sweeps the VCF,
  LFO adds gentle vibrato. All Fundamental (base) modules.
Based on the known-good Demo 1 structure (correct param/port IDs)."""
import json, tarfile, io, subprocess, sys, os

d1 = json.load(open('/tmp/d1.json'))
core = next(m for m in d1['modules'] if m['plugin'] == 'Core')  # verbatim: audio driver 777

def params(pairs):
    return [{"id": i, "value": v} for i, v in pairs]

VER = "2.6.4"
modules = [
    # SEQ3 (Model 104 sequencer): melodic 8-step sequence, all gates on, running.
    {"id": 1, "plugin": "Fundamental", "model": "SEQ3", "version": VER, "pos": [0, 0],
     "params": params([(0, 2.0), (1, 0.0), (2, 0.0), (3, 8.0),
                       (4, 0.0), (5, 0.25), (6, 0.4167), (7, 0.5833),
                       (8, 0.8333), (9, 1.0), (10, 0.5833), (11, 0.25)]
                      + [(i, 0.0) for i in range(12, 36)]),
     "data": {"running": True, "gates": [1, 1, 1, 1, 1, 1, 1, 1], "clockPassthrough": True}},
    # VCO (Model 101 VCO): saw out, a touch of FM depth for the LFO vibrato.
    {"id": 2, "plugin": "Fundamental", "model": "VCO", "version": VER, "pos": [0, 1],
     "params": params([(0, 0), (1, 0), (2, 0.0), (3, 0), (4, 0.04), (5, 0.5), (6, 0), (7, 0)])},
    # VCF (Model 101 resonant VCF): low base cutoff, high resonance, envelope-swept.
    {"id": 3, "plugin": "Fundamental", "model": "VCF", "version": VER, "pos": [14, 1],
     "params": params([(0, 0.28), (1, 0), (2, 0.62), (3, 0.55), (4, 0.5), (5, 0), (6, 0)])},
    # ADSR: punchy plucked envelope.
    {"id": 4, "plugin": "Fundamental", "model": "ADSR", "version": VER, "pos": [24, 1],
     "params": params([(0, 0.02), (1, 0.35), (2, 0.35), (3, 0.3), (4, 0), (5, 0), (6, 0), (7, 0), (8, 0)])},
    # VCA.
    {"id": 5, "plugin": "Fundamental", "model": "VCA-1", "version": VER, "pos": [32, 1],
     "params": params([(0, 1.0), (1, 0)])},
    # LFO for gentle vibrato.
    {"id": 6, "plugin": "Fundamental", "model": "LFO", "version": VER, "pos": [0, 2],
     "params": params([(0, 0), (1, 0), (2, 1.0), (3, 0), (4, 0), (5, 0.5), (6, 0)])},
    core,
]
AUD = core["id"]

def cable(cid, om, oo, im, ii, color):
    return {"id": cid, "outputModuleId": om, "outputId": oo,
            "inputModuleId": im, "inputId": ii, "color": color}

cables = [
    cable(0, 1, 1, 2, 0, "#f3374b"),   # SEQ3 CV0 -> VCO PITCH
    cable(1, 1, 0, 4, 4, "#ffb437"),   # SEQ3 TRIG -> ADSR GATE
    cable(2, 2, 2, 3, 3, "#25c1a1"),   # VCO SAW  -> VCF IN
    cable(3, 3, 0, 5, 1, "#3695ef"),   # VCF LPF  -> VCA IN
    cable(4, 4, 0, 5, 0, "#f3374b"),   # ADSR ENV -> VCA CV   (amp envelope)
    cable(5, 4, 0, 3, 0, "#ffb437"),   # ADSR ENV -> VCF FREQ (filter envelope)
    cable(6, 6, 0, 2, 1, "#8b4ad6"),   # LFO SIN  -> VCO FM   (vibrato)
    cable(7, 5, 0, AUD, 0, "#25c1a1"), # VCA OUT  -> Audio L
    cable(8, 5, 0, AUD, 1, "#3695ef"), # VCA OUT  -> Audio R
]

patch = {"version": d1.get("version", "2.beta.1"),
         "modules": modules, "cables": cables,
         "masterModuleId": AUD}

pj = json.dumps(patch, indent=2).encode()

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/System100.vcv"
tar_bytes = io.BytesIO()
with tarfile.open(fileobj=tar_bytes, mode="w") as tf:
    ti = tarfile.TarInfo("patch.json")
    ti.size = len(pj)
    ti.mode = 0o644
    tf.addfile(ti, io.BytesIO(pj))
tar_bytes.seek(0)
raw = tar_bytes.read()
comp = subprocess.run(["zstd", "-q", "-19", "-c"], input=raw, stdout=subprocess.PIPE, check=True).stdout
open(out, "wb").write(comp)
print("wrote %s (%d bytes, %d modules, %d cables)" % (out, len(comp), len(modules), len(cables)))
