# Undefined geometric normal directions

Paired GEOMETRIC export removes custom normal storage only from disposable calculation meshes, then transports the serialized authored baseline direction by each key's geometric change. Source geometry and custom normals remain unchanged.

The default `undefined_normal_policy` is `REJECT`. A scripted asset may explicitly set `undefined_normal_policy: "BASELINE"` to preserve the serialized authored direction wherever the selected corner's baseline or endpoint geometric direction is zero. A zero direction can arise from a collapsed face or cancelling contributions from coincident opposite faces. This is an explicit exception, not a successful geometric recalculation at that corner.

Each exception is returned in `geometric_normal_fallbacks`, both in the export result and its source trace: object, mesh, public key ID, source vertex, serialized vertex, corner, normal byte offset and `baseline_zero`, `endpoint_zero` or `baseline_and_endpoint_zero`. Debug export preserves these records in `result.json` and the trace. A normal export retains its existing minimal model/sidecar layout; scripting callers can retain the returned report. The regular UI does not opt into this rule.

All selected dynamic keys remain present. No normal delta is emitted for an exception, so the model's authored baseline bytes remain effective. Nonfinite directions, invalid serialized authored normals and defined antiparallel endpoints remain errors. The common normal validator, wire formats and runtime blending are unchanged.

`tools/test_geometric_normal_fallback_blender.py` exercises endpoint cancellation, baseline cancellation after coordinate quantization, strict rejection, exact exception identities, preservation of authored fields, valid geometric changes elsewhere and source restoration. It runs in the Paired capacity stage alongside the existing synthetic export tests. This software validation does not establish visual quality or a numerical acceptance tolerance.
