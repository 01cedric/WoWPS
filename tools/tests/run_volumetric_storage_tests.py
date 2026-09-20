#!/usr/bin/env python3
"""Numerical equivalence and quantified traffic; not a hardware FPS benchmark."""
from pathlib import Path
import json
import numpy as np
rng=np.random.default_rng(273)
f=np.float32
n=1000000
# Test a million positive/negative homogeneous depths, including range clamp.
a=(10**rng.uniform(-7,3,n)).astype(f)
b=(a*(1+rng.uniform(-.003,.003,n))).astype(f)
a[:1000]*=-1
b[1000:2000]*=-1
R=f(160)
old_a=np.minimum(1/np.maximum(a,f(.000001)),R)
old_b=np.minimum(1/np.maximum(b,f(.000001)),R)
tolerance=np.maximum(f(.0005),old_b*f(.0002))
old=np.abs(old_a-old_b)<=tolerance
wa=np.maximum(a,f(1/R));wb=np.maximum(b,f(1/R))
new=np.abs(wb-wa)<=np.maximum(f(.0005)*wa*wb,f(.0002)*wa)
disagree=old!=new
# Float division and multiplication have distinct last-bit rounding. Decisions
# may differ only inside the original gate's roundoff-sized boundary envelope.
boundary_error=np.abs(np.abs(old_a-old_b)-tolerance)/np.maximum(old_b,f(1e-9))
assert np.all(boundary_error[disagree]<f(3e-7))
# Since all radiance is collinear with the single sun/moon source, peak encoding
# changes only half-float rounding. Include highly saturated and weak colors.
color=rng.uniform(0,1,(n,3)).astype(f)
color/=color.max(axis=1)[:,None]
peak=(10**rng.uniform(-5,3,n)).astype(f)
rgb=color*peak[:,None]
old_rgb=rgb.astype(np.float16).astype(f)
new_rgb=peak.astype(np.float16).astype(f)[:,None]*color
error=np.abs(new_rgb-old_rgb).max(axis=1)
assert np.all(error<=peak*f(.001)+f(1.2e-7))
result={'depth_gate_samples':n,'depth_gate_boundary_rounding_disagreements':int(disagree.sum()),'maximum_relative_boundary_error':float(boundary_error[disagree].max(initial=0)), 'color_samples':n,'maximum_peak_relative_color_error':float((error/np.maximum(peak,f(.001))).max()),'scene_pixels':1280*720,'high_taps_per_pixel':34,'per_tap_reciprocals_removed':2,'reciprocal_evaluations_removed_at_720p_high':1280*720*34*2,'intermediate_bytes_per_texel_before':8,'intermediate_bytes_per_texel_after':2,'high_two_slot_intermediate_bytes_before':(1280*720+640*360)*2*8,'high_two_slot_intermediate_bytes_after':(1280*720+640*360)*2*2,'hardware_fps_verified':False}
out=Path(__file__).resolve().parents[2]/'build-test-results/strata';out.mkdir(parents=True,exist_ok=True)
(out/'storage-metrics.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2));print('PASS storage precision and reciprocal-depth gate equivalence within float-roundoff envelope')
