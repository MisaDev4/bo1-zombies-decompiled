// Offline authoring diagnostics. Geometry follows cm_mapkit.cpp; the node grid follows
// G_Mapkit_CollectNodes. Clearance links are conservative estimates, not engine path results.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { zoneRoot as defaultZoneRoot } from './zone-root.mjs';

export const kinds = ['room', 'box', 'stairs', 'spawn', 'zone', 'window', 'spawner', 'perk', 'wallbuy', 'node', 'door', 'debris', 'mysterybox', 'ent', 'powerswitch', 'pap', 'light', 'riser'];
// mapkit-fix-3 mood lighting presets (cm_mapkit.cpp s_lightPresets); tint = color * intensity, 1 = the kit's flat grey
export const lightPresets = { 'dim bunker': { color: [1, 0.82, 0.62], intensity: 0.55 }, 'red alarm': { color: [1, 0.2, 0.14], intensity: 1 }, 'cold lab': { color: [0.72, 0.88, 1], intensity: 1.15 } };
export const lightTint = e => { const p = lightPresets[e.preset] || { color: [1, 1, 1], intensity: 1 }; const c = e.color || p.color, k = e.intensity ?? p.intensity; return c.map(v => Math.min(2, Math.max(0, v * k))); };
export const luminance = t => 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2];
const sides = ['-x', '+x', '-y', '+y'];
const vec = (v, n = 3) => Array.isArray(v) && v.length === n && v.every(Number.isFinite);
export const center = e => e.origin || (e.mins && e.maxs ? e.mins.map((v, i) => (v + e.maxs[i]) / 2) : null);
export const distance = (a, b) => Math.hypot(...a.map((v, i) => v - b[i]));
export const forward = e => [Math.cos((e.yaw ?? (e.kind === 'window' ? 90 : 0)) * Math.PI / 180), Math.sin((e.yaw ?? (e.kind === 'window' ? 90 : 0)) * Math.PI / 180), 0];
export const offset = (p, f, d) => p.map((v, i) => v + f[i] * d);
export const contains = (b, p, eps = 0) => p.every((v, i) => v >= b.mins[i] - eps && v <= b.maxs[i] + eps);
export const editPath = i => `$.edits[${i}]`;
export const editName = (e, i) => e.name || `edit${i}`;
export function finding(code, severity, jsonPath, position, observed, fix) {
  return { code, severity, path: jsonPath, position, observed, fix };
}
export function parseLayout(source) {
  // Preserve line offsets for JSON errors, including comments within strings and escaped quotes.
  return JSON.parse(source.replace(/^\uFEFF/, ' ').replace(/"(?:\\.|[^"\\])*"|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g,
    s => s.startsWith('"') ? s : s.replace(/[^\r\n]/g, ' ')));
}

export function validateSchema(layout) {
  const out = [];
  const bad = (p, what) => out.push(finding('schema', 'FAIL', p, null, what, `Correct ${p}: ${what}.`));
  if (!layout || typeof layout !== 'object' || Array.isArray(layout)) { bad('$', 'expected an object'); return out; }
  if (typeof layout.name !== 'string' || !/^[\w-]{1,63}$/.test(layout.name)) bad('$.name', 'expected 1..63 letters, digits, underscores or hyphens');
  if (layout.base !== 'zombie_pentagon') bad('$.base', 'supported asset base is zombie_pentagon');
  if (layout.empty !== undefined && typeof layout.empty !== 'boolean') bad('$.empty', 'expected a boolean');
  if (!Array.isArray(layout.edits) || !layout.edits.length || layout.edits.length > 1024) { bad('$.edits', 'expected 1..1024 edits'); return out; }
  const names = new Set();
  layout.edits.forEach((e, i) => {
    const p = editPath(i);
    if (!e || typeof e !== 'object' || Array.isArray(e)) { bad(p, 'expected an edit object'); return; }
    if (!kinds.includes(e.kind)) bad(`${p}.kind`, `expected one of ${kinds.join(', ')}`);
    for (const k of ['name', 'room', 'zone', 'from', 'to', 'perk', 'weapon', 'model', 'material', 'floorMaterial', 'wallMaterial', 'ceilingMaterial', 'classname', 'target'])
      if (e[k] !== undefined && (typeof e[k] !== 'string' || !e[k].length || /[\r\n"\x00]/.test(e[k]))) bad(`${p}.${k}`, 'expected a nonempty string without quotes or line breaks');
    if (typeof e.name === 'string') {
      if (names.has(e.name)) bad(`${p}.name`, `duplicate name ${e.name}`);
      names.add(e.name);
    }
    for (const k of ['origin', 'mins', 'maxs', 'spawn', 'angles']) if (e[k] !== undefined && !vec(e[k])) bad(`${p}.${k}`, 'expected three finite numbers');
    const bounds = ['room', 'box', 'stairs', 'zone', 'door', 'debris'].includes(e.kind);
    if (bounds) for (const k of ['mins', 'maxs']) if (e[k] === undefined) bad(`${p}.${k}`, 'required bounds vector');
    if (vec(e.mins) && vec(e.maxs) && e.mins.some((v, a) => v >= e.maxs[a])) bad(`${p}.maxs`, 'each maximum must be greater than its minimum');
    if (['spawn', 'spawner', 'riser', 'perk', 'wallbuy', 'node', 'mysterybox', 'powerswitch', 'pap'].includes(e.kind) || (e.kind === 'window' && layout.empty))
      if (e.origin === undefined) bad(`${p}.origin`, 'required position vector');
    for (const k of ['yaw', 'wall', 'nodeSpacing', 'texScale', 'cost', 'floor', 'ceiling', 'start', 'every'])
      if (e[k] !== undefined && !Number.isFinite(e[k])) bad(`${p}.${k}`, 'expected a finite number');
    for (const k of ['wall', 'texScale', 'every']) if (Number.isFinite(e[k]) && e[k] <= 0) bad(`${p}.${k}`, 'must be positive');
    if (e.cost !== undefined && (e.cost < 0 || !Number.isInteger(e.cost))) bad(`${p}.cost`, 'expected a nonnegative integer');
    if (e.nodeSpacing !== undefined && e.nodeSpacing !== 0 && e.nodeSpacing < 16) bad(`${p}.nodeSpacing`, 'use 0 to disable nodes or a spacing of at least 16 units');
    if (e.kind === 'light') {
      if (e.preset !== undefined && !Object.hasOwn(lightPresets, e.preset)) bad(`${p}.preset`, `expected one of ${Object.keys(lightPresets).join(', ')}`);
      if (e.color !== undefined && (!vec(e.color) || e.color.some(v => v < 0 || v > 2))) bad(`${p}.color`, 'expected three numbers 0..2 (1 1 1 = neutral)');
      if (e.intensity !== undefined && (!Number.isFinite(e.intensity) || e.intensity < 0 || e.intensity > 2)) bad(`${p}.intensity`, 'expected a number 0..2 (1 = the flat grey)');
      if (e.room === undefined && (e.mins === undefined || e.maxs === undefined)) bad(`${p}.mins`, 'a light needs mins/maxs or room (the name of a room, zone or box edit)');
    }
    if (e.power !== undefined && typeof e.power !== 'boolean') bad(`${p}.power`, 'expected a boolean');
    if (e.contents !== undefined && !['solid', 'none'].includes(e.contents)) bad(`${p}.contents`, 'supported contents are solid and none (other names become solid in the engine)');
    // mapkit-fix-4c: window "open": true = unboardable entrance; risers are retail zone spawners (both empty base only)
    if (e.kind === 'window' && e.open !== undefined) {
      if (typeof e.open !== 'boolean') bad(`${p}.open`, 'expected a boolean (true = open entrance: no boards, no rebuild)');
      else if (e.open && !layout.empty) bad(`${p}.open`, 'open entrances need the empty base ("empty": true)');
    } else if (e.kind === 'riser' && !layout.empty) bad(`${p}.kind`, 'risers need the empty base ("empty": true)');
    else if (e.open !== undefined && (!Array.isArray(e.open) || e.open.some(s => !sides.includes(s)))) bad(`${p}.open`, 'expected an array of -x, +x, -y, +y');
    if (e.gaps !== undefined && !Array.isArray(e.gaps)) bad(`${p}.gaps`, 'expected an array');
    if (Array.isArray(e.gaps)) {
      for (const side of sides) if (e.gaps.filter(g => g?.side === side).length > 8) bad(`${p}.gaps`, `engine supports at most 8 gaps on ${side}`);
      e.gaps.forEach((g, j) => {
        const q = `${p}.gaps[${j}]`;
        if (!g || !sides.includes(g.side)) bad(`${q}.side`, 'expected -x, +x, -y or +y');
        for (const k of ['along', 'z']) if (!vec(g?.[k], 2) || g[k][0] >= g[k][1]) bad(`${q}.${k}`, 'expected two increasing finite numbers');
        if (g && sides.includes(g.side) && vec(g.along, 2) && vec(g.z, 2) && vec(e.mins) && vec(e.maxs)) {
          const a = g.side.endsWith('x') ? 1 : 0;
          if (g.along[0] < e.mins[a] - (e.wall ?? 8) || g.along[1] > e.maxs[a] + (e.wall ?? 8) || g.z[0] < e.mins[2] || g.z[1] > e.maxs[2]) bad(q, 'gap extends beyond its wall');
        }
      });
    }
    if (['zone', 'room'].includes(e.kind) && !e.name) bad(`${p}.name`, 'required unique name');
    if (e.kind === 'perk' && !e.perk) bad(`${p}.perk`, 'required perk identifier');
    if (['spawn', 'spawner', 'riser'].includes(e.kind) && !e.zone) bad(`${p}.zone`, 'required zone name');
    if (['door', 'debris'].includes(e.kind)) for (const k of ['from', 'to']) if (!e[k]) bad(`${p}.${k}`, 'required zone name');
  });
  return out;
}

// Fastfile strings are a presence catalog, not proof that a typed asset loaded successfully.
// Purchases and load diagnostics remain mandatory live checks. No Steam files are written.
// zoneRoot: the install's zone folder (see zone-root.mjs).
export function loadCatalog(zoneRoot = defaultZoneRoot()) {
  const strings = new Set(), materials = new Set(), sources = [];
  for (const name of ['common_zombie', 'zombie_pentagon']) {
    const file = fs.readdirSync(zoneRoot).map(d => path.join(zoneRoot, d, `${name}.ff`)).find(f => fs.existsSync(f));
    if (!file) throw new Error(`Cannot find ${name}.ff in ${zoneRoot}`);
    const data = fs.readFileSync(file);
    let unpacked;
    for (let off = 8; off < 64 && !unpacked; off++) if (data[off] === 0x78) {
      try { unpacked = zlib.inflateSync(data.subarray(off), { finishFlush: zlib.constants.Z_SYNC_FLUSH }); } catch { /* try next header */ }
    }
    if (!unpacked) throw new Error(`Cannot inflate catalog ${file}`);
    const text = unpacked.toString('latin1');
    for (const m of text.matchAll(/(?:^|\x00)([a-zA-Z0-9_/.+-]{2,160})(?=\x00)/g)) strings.add(m[1]);
    for (const m of text.matchAll(/mc\/[a-z0-9_]+/g)) materials.add(m[0]);
    sources.push(file);
  }
  return { strings, materials, sources };
}

export function geometry(layout) {
  const boxes = [], nodes = [], openings = [];
  const addBox = (mins, maxs, edit, surface, contents) => {
    if (mins.every((v, i) => v < maxs[i])) boxes.push({ mins: [...mins], maxs: [...maxs], edit, surface, contents: contents ?? 'solid' });
  };
  layout.edits.forEach((e, edit) => {
    if (e.kind === 'box') addBox(e.mins, e.maxs, edit, 'box', e.contents);
    if (e.kind === 'node') nodes.push({ origin: [...e.origin], edit, type: 1 });
    // Mirror CM_Mapkit_StairsStep / CM_Mapkit_WalkNodes: step boxes, tread nodes and walkable box tops (feet Z).
    if (e.kind === 'stairs') for (const st of stairsSteps(e)) {
      addBox(st.mins, st.maxs, edit, 'stairs', 'solid');
      for (const c of gridAxis(st.mins[st.across], st.maxs[st.across], e.nodeSpacing ?? 56)) {
        const o = [0, 0, st.maxs[2]]; o[st.axis] = (st.mins[st.axis] + st.maxs[st.axis]) / 2; o[st.across] = c;
        nodes.push({ origin: o, edit, type: 1 });
      }
    }
    if (e.kind === 'box' && e.walkable) for (const x of gridAxis(e.mins[0], e.maxs[0], e.nodeSpacing ?? 56)) for (const y of gridAxis(e.mins[1], e.maxs[1], e.nodeSpacing ?? 56))
      nodes.push({ origin: [x, y, e.maxs[2]], edit, type: 1 });
    if (e.kind !== 'room') return;
    const [lo, hi, t] = [e.mins, e.maxs, e.wall ?? 8];
    if (e.floor !== 0) addBox([lo[0] - t, lo[1] - t, lo[2] - t], [hi[0] + t, hi[1] + t, lo[2]], edit, 'floor', e.contents);
    if (e.ceiling !== 0) addBox([lo[0] - t, lo[1] - t, hi[2]], [hi[0] + t, hi[1] + t, hi[2] + t], edit, 'ceiling', e.contents);
    for (const side of sides) {
      const axis = side.endsWith('x') ? 0 : 1, along = axis ^ 1;
      const a = [...lo], b = [...hi];
      a[axis] = side[0] === '+' ? hi[axis] : lo[axis] - t;
      // mapkit-fix-3c: mirrors cm_mapkit.cpp - no corner extension toward an open side (it z-fought the neighbour's shared wall).
      b[axis] = a[axis] + t; a[along] -= e.open?.includes(along ? '-y' : '-x') ? 0 : t; b[along] += e.open?.includes(along ? '+y' : '+x') ? 0 : t;
      const gaps = e.open?.includes(side) ? [{ along: [a[along], b[along]], z: [lo[2], hi[2]], open: true }] : (e.gaps || []).filter(g => g.side === side).sort((x, y) => x.along[0] - y.along[0]);
      let cursor = a[along];
      for (const g of gaps) {
        const p = [...a], q = [...b]; p[along] = cursor; q[along] = g.along[0];
        addBox(p, q, edit, 'wall', e.contents);
        p[along] = g.along[0]; q[along] = g.along[1]; q[2] = g.z[0];
        addBox(p, q, edit, 'wall', e.contents);
        p[2] = g.z[1]; q[2] = b[2]; addBox(p, q, edit, 'wall', e.contents);
        openings.push({ edit, side, axis, along, mins: a.map((v, k) => k === along ? g.along[0] : k === 2 ? g.z[0] : v), maxs: b.map((v, k) => k === along ? g.along[1] : k === 2 ? g.z[1] : v), open: !!g.open });
        cursor = Math.max(cursor, g.along[1]);
      }
      const p = [...a]; p[along] = cursor; addBox(p, b, edit, 'wall', e.contents);
    }
    const spacing = e.nodeSpacing ?? 56;
    if (spacing > 0) {
      const counts = [0, 1].map(a => Math.max(1, Math.floor((hi[a] - lo[a] - 48) / spacing) + 1));
      if (nodes.length + counts[0] * counts[1] > 8192) throw new Error(`Node limit 8192 exceeded at ${editPath(edit)}.nodeSpacing; increase spacing.`);
      for (let y = 0; y < counts[1]; y++) for (let x = 0; x < counts[0]; x++)
        nodes.push({ origin: [x, y].map((v, a) => counts[a] > 1 ? lo[a] + 24 + (hi[a] - lo[a] - 48) * v / (counts[a] - 1) : (lo[a] + hi[a]) / 2).concat(lo[2]), edit, type: 1 });
    }
  });
  // Match Mapkit_CollectNodes: omit automatic candidates obstructed by static boxes.
  // Explicit nodes and unsupported candidates still receive normal diagnostics.
  const omittedNodes = nodes.filter(n => layout.edits[n.edit].kind === 'room' && segmentBlocked(n.origin, n.origin, boxes, 16));
  return { boxes, nodes: nodes.filter(n => !omittedNodes.includes(n)), omittedNodes, openings };
}

export function stairsSteps(e) {
  if (!Array.isArray(e.mins) || !Array.isArray(e.maxs) || e.mins.some((v, a) => v >= e.maxs[a])) return [];
  const up = e.up ?? '+x', axis = up[1] === 'y' ? 1 : 0, sign = up[0] === '-' ? -1 : 1, rise = e.rise ?? 12;
  const n = Math.min(32, Math.max(1, Math.floor((e.maxs[2] - e.mins[2]) / Math.max(rise, 1) + 0.5)));
  const tread = (e.maxs[axis] - e.mins[axis]) / n;
  return Array.from({ length: n }, (_, i) => {
    const mins = [...e.mins], maxs = [...e.maxs];
    mins[axis] = sign > 0 ? e.mins[axis] + tread * i : e.maxs[axis] - tread * (i + 1);
    maxs[axis] = mins[axis] + tread;
    maxs[2] = e.mins[2] + (e.maxs[2] - e.mins[2]) * (i + 1) / n;
    return { mins, maxs, axis, across: axis ^ 1, rise: (e.maxs[2] - e.mins[2]) / n, tread };
  });
}

function gridAxis(lo, hi, spacing) {
  const a = lo + 24, b = hi - 24;
  if (b <= a || spacing <= 0) return [(lo + hi) / 2];
  let n = Math.floor((b - a) / spacing) + 1;
  if (a + spacing * (n - 1) < b - 0.5) n++;
  n = Math.min(n, 16);
  return Array.from({ length: n }, (_, i) => n === 1 ? (a + b) / 2 : a + (b - a) * i / (n - 1));
}

export function purchaseTriggers(edits) {
  return edits.map(e => {
    let mins, maxs;
    if (e.kind === 'wallbuy' && e.origin) {
      const r = (e.yaw ?? 0) * Math.PI / 180, c = [e.origin[0] + Math.cos(r) * 24, e.origin[1] + Math.sin(r) * 24, e.origin[2]];
      mins = c.map(v => v - 32); maxs = c.map(v => v + 32);
    } else if (['perk', 'pap', 'powerswitch'].includes(e.kind) && e.origin) { // pap / powerswitch: the perk trigger (_mapkit.gsc mapkit_front_trigger)
      // yaw unknown (snap:"wall"): the cylinder may sit 12 either way, so widen the radius instead
      const r = (e.yaw ?? 0) * Math.PI / 180, off = e.yaw === undefined ? 0 : 12;
      const c = [e.origin[0] + Math.cos(r) * off, e.origin[1] + Math.sin(r) * off];
      return { e, circle: { c, r: e.yaw === undefined ? 56 : 44 }, mins: [c[0], c[1], e.origin[2] + 48], maxs: [c[0], c[1], e.origin[2] + 118] };
    } else if (e.kind === 'mysterybox' && e.origin) {
      mins = [e.origin[0] - 40, e.origin[1] - 40, e.origin[2]]; maxs = [e.origin[0] + 40, e.origin[1] + 40, e.origin[2] + 64];
    }
    return mins && { e, mins, maxs };
  }).filter(Boolean);
}

// Horizontal separation of two purchase triggers (negative = they intersect); +Infinity when their heights do not overlap.
export function triggerGap(a, b) {
  if (a.mins[2] > b.maxs[2] || b.mins[2] > a.maxs[2]) return Infinity;
  if (a.circle && b.circle) return Math.hypot(a.circle.c[0] - b.circle.c[0], a.circle.c[1] - b.circle.c[1]) - a.circle.r - b.circle.r;
  if (a.circle || b.circle) {
    const [c, q] = a.circle ? [a.circle, b] : [b.circle, a];
    const d = [0, 1].map(k => Math.max(q.mins[k] - c.c[k], 0, c.c[k] - q.maxs[k]));
    return (d[0] || d[1]) ? Math.hypot(d[0], d[1]) - c.r : -c.r - Math.min(...[0, 1].map(k => Math.min(c.c[k] - q.mins[k], q.maxs[k] - c.c[k])));
  }
  const g = [0, 1].map(k => Math.max(a.mins[k] - b.maxs[k], b.mins[k] - a.maxs[k]));
  return g[0] < 0 && g[1] < 0 ? Math.max(g[0], g[1]) : Math.hypot(Math.max(g[0], 0), Math.max(g[1], 0));
}

// AABB sweep against the standing hull; actor physics and slope handling are checked live.
export function segmentBlocked(a, b, boxes, radius = 15, height = 72) {
  return boxes.some(box => {
    if (box.contents === 'none') return false;
    const lo = [box.mins[0] - radius, box.mins[1] - radius, box.mins[2] - height + 0.25];
    const hi = [box.maxs[0] + radius, box.maxs[1] + radius, box.maxs[2] - 0.25];
    let first = 0, last = 1;
    for (let k = 0; k < 3; k++) {
      const d = b[k] - a[k];
      if (Math.abs(d) < 1e-8) { if (a[k] <= lo[k] || a[k] >= hi[k]) return false; }
      else {
        let t = (lo[k] - a[k]) / d, u = (hi[k] - a[k]) / d;
        if (t > u) [t, u] = [u, t];
        first = Math.max(first, t); last = Math.min(last, u);
        if (first > last) return false;
      }
    }
    return true;
  });
}

export function analyzeStatic(layout, catalog) {
  const findings = validateSchema(layout);
  if (findings.length) return { findings, windows: [], zones: [], nodes: [], boxes: [] };
  const edits = layout.edits.map((e, i) => ({ ...e, i }));
  const rooms = edits.filter(e => e.kind === 'room'), zones = edits.filter(e => e.kind === 'zone');
  const windows = edits.filter(e => e.kind === 'window'), spawners = edits.filter(e => e.kind === 'spawner');
  const add = (code, e, field, observed, fix, severity = 'FAIL', position = center(e)) => findings.push(finding(code, severity, `${editPath(e.i)}${field ? '.' + field : ''}`, position, observed, fix));
  for (const e of edits) {
    for (const key of ['material', 'floorMaterial', 'wallMaterial', 'ceilingMaterial', 'model', 'weapon']) if (e[key] && catalog && !(key.toLowerCase().includes('material') ? catalog.materials : catalog.strings).has(e[key]))
      add('asset-unknown', e, key, `${e[key]} is absent from the Five/common_zombie presence catalog.`, `Choose a ${key} from MAPKIT.md's tested catalog or tools/mapkit/materials.mjs; verify custom assets are supplied by the mod.`, 'WARN');
    if (e.perk && !['specialty_quickrevive', 'specialty_armorvest', 'specialty_fastreload', 'specialty_rof'].includes(e.perk))
      add('perk-unknown', e, 'perk', `The mapkit runtime skips perk ${e.perk}.`, 'Use specialty_quickrevive, specialty_armorvest, specialty_fastreload or specialty_rof.');
    if (e.zone && !zones.some(z => z.name === e.zone)) add('zone-missing', e, 'zone', `No zone named ${e.zone}.`, `Add a zone edit named ${e.zone} around the intended play room, or correct zone to an existing name.`);
    if (e.kind === 'ent') add('custom-entity', e, '', 'Arbitrary entity geometry and script behavior cannot be inferred offline.', 'Review the entity fields and live script/load diagnostics.', 'WARN');
  }
  // mapkit-fix-3 mood lighting: readable = luminance of the tint 0.3..1.6 (1 = the flat grey; the sun still adds shading)
  for (const e of edits.filter(e => e.kind === 'light')) {
    const area = e.room !== undefined ? edits.find(r => r !== e && r.name === e.room && vec(r.mins) && vec(r.maxs)) : e;
    if (!area) { add('light-room', e, 'room', `No room, zone or box edit named ${e.room} with mins/maxs.`, 'Set room to the name of a room edit, or give the light its own mins/maxs.'); continue; }
    const t = lightTint(e), l = luminance(t);
    if (!layout.empty) add('light-overlay', e, 'kind', 'Mood lights tint the empty base only; on the retail overlay the retail light grid lights this area.', 'Use "empty": true or remove the light.', 'WARN', center(area));
    else if (l < 0.3) add('light-dark', e, e.intensity !== undefined ? 'intensity' : 'color', `Tint ${t.map(v => v.toFixed(2)).join(' ')} has luminance ${l.toFixed(2)} (< 0.30): walls, zombies and buy prompts read as black.`, 'Raise intensity, or use a brighter color (the "dim bunker" preset is 0.46).', 'WARN', center(area));
    else if (l > 1.6 || t.some(v => v >= 2)) add('light-bright', e, e.intensity !== undefined ? 'intensity' : 'color', `Tint ${t.map(v => v.toFixed(2)).join(' ')} has luminance ${l.toFixed(2)} (> 1.60 or a channel at the 2.0 cap): textures wash out to flat colour.`, 'Lower intensity (1 = the flat grey, "cold lab" is 0.98).', 'WARN', center(area));
  }
  if (!layout.empty) {
    findings.push(finding('retail-overlay', 'WARN', '$.empty', null, 'Retail BSP geometry is unavailable to offline layout checks.', 'Use the live dump to inspect retail collision and nodes; empty:true enables full authored-geometry checks.'));
    return { findings, windows, zones, nodes: [], boxes: [] };
  }
  const geo = geometry(layout), { boxes, nodes, openings } = geo;
  if (boxes.length > 512) findings.push(finding('box-limit', 'FAIL', '$.edits', null, `${boxes.length} boxes exceed the engine limit of 512; later boxes are dropped.`, 'Reduce room wall/gap pieces and decorative boxes to at most 512.'));
  const solid = boxes.filter(b => b.contents !== 'none');
  const floorAt = (p, maxDrop = 18) => solid.filter(b => p[0] >= b.mins[0] && p[0] <= b.maxs[0] && p[1] >= b.mins[1] && p[1] <= b.maxs[1] && b.maxs[2] <= p[2] + 0.5 && b.maxs[2] >= p[2] - maxDrop).sort((a, b) => b.maxs[2] - a.maxs[2])[0];
  const supported = p => !!floorAt(p);
  // Step-up walk like the engine's actor physics (g_apl.stepheight 18): every 8 units the feet Z becomes the highest
  // solid top under the hull footprint within 18 up/down; the standing hull must be clear there and the centre supported.
  const canWalk = (a, b, radius = 15) => {
    if (Math.abs(a[2] - b[2]) > 18) return false;
    const steps = Math.max(1, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / 8));
    let z = a[2];
    for (let k = 0; k <= steps; k++) {
      const x = a[0] + (b[0] - a[0]) * k / steps, y = a[1] + (b[1] - a[1]) * k / steps;
      const top = solid.filter(q => x + radius > q.mins[0] && x - radius < q.maxs[0] && y + radius > q.mins[1] && y - radius < q.maxs[1] && q.maxs[2] <= z + 18.5 && q.maxs[2] >= z - 18)
        .reduce((m, q) => Math.max(m, q.maxs[2]), -Infinity);
      if (top === -Infinity) return false;
      z = top;
      if (!supported([x, y, z]) || segmentBlocked([x, y, z], [x, y, z], solid, radius)) return false;
    }
    return Math.abs(z - b[2]) <= 18;
  };
  for (const r of rooms) {
    // Exact planar decomposition, so even a narrow uncovered strip is found (not sampled away).
    const floors = solid.filter(b => Math.abs(b.maxs[2] - r.mins[2]) <= 0.5 && b.maxs[0] > r.mins[0] && b.mins[0] < r.maxs[0] && b.maxs[1] > r.mins[1] && b.mins[1] < r.maxs[1]);
    const cuts = [0, 1].map(a => [...new Set([r.mins[a], r.maxs[a], ...floors.flatMap(b => [Math.max(r.mins[a], b.mins[a]), Math.min(r.maxs[a], b.maxs[a])])])].sort((a, b) => a - b));
    let hole;
    for (let x = 1; (r.floor === 0 || r.contents === 'none') && x < cuts[0].length && !hole; x++) for (let y = 1; y < cuts[1].length && !hole; y++) {
      const p = [(cuts[0][x - 1] + cuts[0][x]) / 2, (cuts[1][y - 1] + cuts[1][y]) / 2, r.mins[2]];
      if (!floorAt(p, 0.5)) hole = { p, lo: [cuts[0][x - 1], cuts[1][y - 1], r.mins[2] - 8], hi: [cuts[0][x], cuts[1][y], r.mins[2]] };
    }
    if (hole) add('floor-hole', r, 'floor', `Uncovered floor area ${JSON.stringify(hole.lo)}..${JSON.stringify(hole.hi)}.`, `Set ${editPath(r.i)}.floor to 1, or add a solid box with mins ${JSON.stringify(hole.lo)} and maxs ${JSON.stringify(hole.hi)} (then check for remaining holes).`, 'FAIL', hole.p);
    for (const other of rooms.filter(o => o.i > r.i)) if ([0, 1, 2].every(a => Math.min(r.maxs[a], other.maxs[a]) > Math.max(r.mins[a], other.mins[a])))
      add('room-overlap', other, 'mins', `Usable interior overlaps ${editPath(r.i)} (${r.name}).`, `Move ${other.name}'s mins/maxs so the interiors meet only at a shared wall or opening.`);
  }
  for (const e of edits.filter(e => e.kind === 'box' && e.contents !== 'none')) {
    const overlap = boxes.find(b => b.edit !== e.i && b.surface === 'wall' && [0, 1, 2].every(a => Math.min(b.maxs[a], e.maxs[a]) - Math.max(b.mins[a], e.mins[a]) > 0.5));
    if (overlap) add('solid-overlap', e, 'mins', `Solid box intersects a wall of ${editPath(overlap.edit)}.`, 'Resize/move this box so its bounds stop at the room interior face; use contents:"none" only if it is intentionally noncolliding decoration.', 'WARN');
  }
  for (const e of edits.filter(e => e.origin && !['window', 'node', 'wallbuy', 'ent'].includes(e.kind))) {
    const p = offset(e.origin, [0, 0, 1], 1);
    const hit = solid.find(b => contains(b, p) && b.maxs[2] > p[2]);
    if (hit) add(e.kind === 'spawner' ? 'spawner-in-wall' : 'item-in-wall', e, 'origin', `${e.kind} is inside ${editPath(hit.edit)} ${hit.surface}.`, `Move origin into clear room space, at least 24 units from the wall/solid ${editPath(hit.edit)}; keep origin.z at floor height.`);
    if (!floorAt(e.origin, 2)) add('item-in-air', e, 'origin', `${e.kind} has no floor within 2 units below its origin.`, `Set origin.z to the supporting room's mins[2] and place XY above its floor (perks snap down at runtime).`, e.kind === 'perk' ? 'WARN' : 'FAIL');
  }
  for (const e of edits.filter(e => e.kind === 'wallbuy')) {
    const hit = solid.find(b => contains(b, e.origin) && b.surface !== 'floor');
    if (hit) add('item-in-wall', e, 'origin', `Wall-buy model is embedded in ${editPath(hit.edit)} ${hit.surface}.`, 'Move origin 4 units inward from the wall face, set yaw into the room, and keep the purchase approach clear.');
  }
  const zoneAt = p => zones.find(z => contains(z, p));
  // mapkit-fix-4c: retail do_zombie_rise picks a spot of the riser's own zone (zone.rise_locations): it must be inside it
  for (const r of edits.filter(e => e.kind === 'riser' && vec(e.origin))) {
    const z = zones.find(z => z.name === r.zone);
    if (!z) add('riser-zone', r, 'zone', `Riser zone ${r.zone} is not a zone edit.`, 'Name an existing zone edit; the riser spawns only while that zone is active.');
    else if (!contains(z, offset(r.origin, [0, 0, 1], 1))) add('riser-zone', r, 'origin', `Riser is outside its zone ${r.zone}.`, `Move the origin inside ${r.zone}'s mins/maxs or name the zone it stands in.`);
  }
  for (const w of windows) {
    const f = forward(w), inside = offset(w.origin, f, 48); inside[2] += 32;
    w.zone = zoneAt(inside)?.name;
    const gap = openings.find(g => contains(g, offset(w.origin, [0, 0, 1], 64), 1) && Math.abs(f[g.axis]) > 0.99);
    if (!gap || gap.open) add('window-wall', w, 'origin', 'Window does not sit in a bounded wall gap with yaw facing across it.', 'Place origin at the wall centre, yaw into the room; author a 100-unit gap along the wall with z [origin.z+36, origin.z+104].');
    else if (gap.maxs[gap.along] - gap.mins[gap.along] < 80 || gap.mins[2] > w.origin[2] + 36 || gap.maxs[2] < w.origin[2] + 104)
      add('window-gap-size', w, 'origin', 'Wall gap is too small for the boarded-window prefab.', `Set the matching ${editPath(gap.edit)}.gaps entry to width 100 centred on the window and z [${w.origin[2] + 36},${w.origin[2] + 104}].`);
    if (!w.zone) add('window-zone', w, 'yaw', 'No zone contains origin + forward*48 + [0,0,32].', 'Point yaw into the play room and extend that zone mins/maxs to contain the interior landing.');
    if (w.room && !rooms.some(r => r.name === w.room && contains(r, inside))) add('window-room', w, 'room', `${w.room} does not contain the interior window landing.`, `Set room to the room containing ${JSON.stringify(inside)} or correct origin/yaw.`);
    w.exterior = offset(w.origin, f, -43); w.interior = offset(w.origin, f, 37);
  }
  // Resolve all zones before nearest-window ownership (layout ordering must not affect attribution).
  for (const w of windows) {
    w.spawners = spawners.filter(s => s.zone === w.zone && windows.filter(q => q.zone === s.zone).sort((a, b) => distance(s.origin, a.origin) - distance(s.origin, b.origin))[0]?.i === w.i).map(s => s.i);
    if (!w.spawners.length) add('window-spawner', w, 'origin', `No outside spawner is assigned to ${editName(w, w.i)}.`, `Add a spawner behind the window at ${JSON.stringify(offset(w.origin, forward(w), -160))}, zone ${w.zone || '<interior zone>'}; make this its nearest window in that zone.`);
    for (const si of w.spawners) if (edits[si].origin.reduce((d, v, a) => d + (v - w.origin[a]) * forward(w)[a], 0) >= 0) add('spawner-inside', edits[si], 'origin', `Spawner is on the player side of ${editPath(w.i)}.`, `Move origin behind ${editName(w, w.i)} along -forward, onto its exterior floor.`);
    const outside = nodes.filter(n => distance(n.origin, w.exterior) <= 192 && canWalk(n.origin, w.exterior, 16));
    const exteriorFloor = floorAt(w.exterior, 128);
    if (exteriorFloor && Math.abs(exteriorFloor.maxs[2] - w.origin[2]) > 1)
      add('window-floor-height', w, 'origin', `Exterior floor top z=${exteriorFloor.maxs[2]} differs from the window floor z=${w.origin[2]}.`, `Align ${editPath(exteriorFloor.edit)} floor top with z=${w.origin[2]} (room mins[2] or floor box maxs[2]); keep exterior node origins on that floor.`, 'WARN', w.exterior);
    if (!outside.length) {
      const yard = rooms.find(r => contains(r, w.exterior, 1));
      add('window-exterior-node', w, 'origin', `No exterior walking node connects to traverse begin ${JSON.stringify(w.exterior)}.`, yard ? `Set ${editPath(yard.i)}.nodeSpacing to 64 or add a node at ${JSON.stringify(w.exterior)}; keep its floor at z=${w.origin[2]} and clear the approach.` : `Add an exterior room/floor under ${JSON.stringify(w.exterior)} at z=${w.origin[2]}, with nodeSpacing 64 and a clear approach.`);
    }
    if (!nodes.some(n => distance(n.origin, w.interior) <= 192 && canWalk(n.origin, w.interior, 16))) add('window-interior-node', w, 'origin', 'Traverse landing has no clear connection to the interior graph.', `Clear solids around ${JSON.stringify(w.interior)}, provide floor at z=${w.origin[2]}, and enable room nodeSpacing 64.`);
  }
  for (const d of edits.filter(e => ['door', 'debris'].includes(e.kind))) {
    const from = zones.find(z => z.name === d.from), to = zones.find(z => z.name === d.to);
    if (!from || !to || from === to) add('door-zone', d, !to ? 'to' : 'from', `Door does not join two distinct existing zones (${d.from} -> ${d.to}).`, `Set from/to to two adjacent zone names, or add a zone behind the door with mins/maxs enclosing the destination room. Remove this door edit if no destination room is intended.`);
    else {
      const p = center(d); p[2] = d.mins[2] + 32;
      if (!contains(from, offset(p, forward(d), -48)) || !contains(to, offset(p, forward(d), 48))) add('door-separation', d, 'yaw', 'Opposite sides of the door do not lie in its from/to zones.', `Move mins/maxs into their shared doorway and set yaw from ${d.from} toward ${d.to}; each side 48 units away must be in the corresponding zone.`);
    }
  }
  for (const g of openings) {
    const r = edits[g.edit], p = center(g), sign = g.side[0] === '+' ? 1 : -1;
    p[2] = r.mins[2] + 40;
    // An open yard side may be backed by the adjoining room's window wall.
    p[g.axis] += sign * 8;
    const hasWindow = windows.some(w => contains(g, offset(w.origin, [0, 0, 1], 64), 1));
    if (!hasWindow && !rooms.some(o => o.i !== r.i && contains(o, p, o.wall ?? 8)) && !solid.some(b => contains(b, p)))
      add('room-open', r, g.open ? 'open' : 'gaps', `Opening on ${g.side} leads outside all authored rooms/walls.`, g.open ? `Remove "${g.side}" from open, or add a connecting room with a supported floor and enclosing outer walls.` : `Close this ${g.side} gap, or add its intended window/exterior room and supporting floor.`, 'WARN', p);
  }
  const spawn = edits.find(e => e.kind === 'spawn');
  if (!spawn) findings.push(finding('spawn-missing', 'FAIL', '$.edits', null, 'No player spawn.', 'Add a spawn edit on a room floor with its zone name.'));
  nodes.forEach((n, i) => { n.i = i; n.links = []; n.clear = !segmentBlocked(n.origin, n.origin, solid, 16) && supported(n.origin); });
  const badNodes = nodes.filter(n => !n.clear);
  // Summarize per authoring edit, rather than flooding the report with every obstructed grid point.
  for (const index of new Set(badNodes.map(n => n.edit))) {
    const bad = badNodes.filter(n => n.edit === index);
    const obstacle = solid.find(b => segmentBlocked(bad[0].origin, bad[0].origin, [b], 16));
    add('node-clearance', edits[index], edits[index].kind === 'node' ? 'origin' : 'nodeSpacing', `${bad.length} generated/authored node(s) lack zombie clearance or floor; first at ${JSON.stringify(bad[0].origin)}${obstacle ? `, obstruction ${editPath(obstacle.edit)}` : ''}.`, obstacle ? `Move/resize ${editPath(obstacle.edit)} away from the node by at least 16 units, or move this explicit node onto clear floor.` : 'Restore floor below these nodes or move explicit nodes onto supported floor.', 'WARN', bad[0].origin);
  }
  const buckets = new Map();
  for (const n of nodes.filter(n => n.clear)) {
    const key = n.origin.slice(0, 2).map(v => Math.floor(v / 192)).join(',');
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(n);
  }
  for (const n of nodes.filter(n => n.clear)) {
    const [x, y] = n.origin.slice(0, 2).map(v => Math.floor(v / 192));
    for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) for (const m of buckets.get(`${x + dx},${y + dy}`) || [])
      if (m.i > n.i && distance(n.origin, m.origin) <= 192 && canWalk(n.origin, m.origin, 16)) { n.links.push(m.i); m.links.push(n.i); }
  }
  const near = p => nodes.filter(n => n.clear && distance(n.origin, p) < 192 && canWalk(n.origin, p, 16));
  const doors = edits.filter(e => ['door', 'debris'].includes(e.kind));
  for (const d of doors) {
    const from = zones.find(z => z.name === d.from), to = zones.find(z => z.name === d.to);
    if (!from || !to || from === to) continue;
    const seeds = nodes.filter(n => n.clear && contains(from, n.origin) && !segmentBlocked(n.origin, n.origin, doors, 16));
    const visited = new Set(seeds.map(n => n.i)), pending = [...visited];
    for (let k = 0; k < pending.length; k++) for (const j of nodes[pending[k]].links)
      if (!visited.has(j) && !segmentBlocked(nodes[pending[k]].origin, nodes[j].origin, doors, 16)) { visited.add(j); pending.push(j); }
    if (nodes.some(n => visited.has(n.i) && contains(to, n.origin))) add('door-bypass', d, 'mins', 'The estimated walking graph can enter the destination zone while all door/debris boxes are closed.', 'Resize the blocker to fill its entire doorway, close the unintended side gap, or correct from/to so the purchased door gates the intended zone.', 'WARN');
  }
  for (const w of windows) {
    const exterior = new Set(near(w.exterior).map(n => n.i)), pending = [...exterior];
    for (let k = 0; k < pending.length; k++) for (const j of nodes[pending[k]].links) if (!exterior.has(j)) { exterior.add(j); pending.push(j); }
    for (const si of w.spawners) if (!near(edits[si].origin).some(n => exterior.has(n.i)))
      add('spawner-disconnected', edits[si], 'origin', `Spawner has no estimated exterior-node route to ${editName(w, w.i)}.`, `Provide connected floor and clear nodes from this spawner to ${JSON.stringify(w.exterior)}; enable the exterior room's nodeSpacing or add explicit nodes.`);
  }
  const reached = new Set(spawn ? near(spawn.origin).map(n => n.i) : []), queue = [...reached];
  for (let k = 0; k < queue.length; k++) for (const j of nodes[queue[k]].links) if (!reached.has(j)) { reached.add(j); queue.push(j); }
  for (const z of zones) {
    z.staticReachable = nodes.some(n => reached.has(n.i) && contains(z, offset(n.origin, [0, 0, 1], 1)));
    if (!z.staticReachable) add('zone-disconnected', z, 'mins', 'No clear estimated walking-node route from player spawn with blockers opened.', 'Provide floor, a doorway at least 32 units wide and 72 units high, and clear nodes joining this zone to the spawn zone. Confirm with the live graph.');
  }
  // mapkit-fix-3 power: one switch per layout (the runtime skips the rest); "power" is a door field (retail debris has no
  // power case); without a switch the empty base has power from the start, so a power door is just a buyable door.
  const switches = edits.filter(e => e.kind === 'powerswitch');
  for (const e of switches.slice(1))
    add('powerswitch-extra', e, 'kind', `The layout has ${switches.length} power switches; the runtime places only the first (${editPath(switches[0].i)}).`, 'Keep one powerswitch edit.');
  for (const e of edits.filter(e => e.power === true)) {
    if (e.kind !== 'door') add('power-ignored', e, 'power', `"power" on a ${e.kind} is ignored: retail debris and other kinds have no power case.`, 'Put "power": true on a door edit, or remove it.', 'WARN');
    else if (!switches.length) add('power-no-switch', e, 'power', 'The door waits for power, but without a powerswitch edit the power is on from the start, so it is an ordinary buyable door.', 'Add a powerswitch edit (see MAPKIT.md "Power and Pack-a-Punch") or remove "power".', 'WARN');
  }
  for (const e of edits.filter(e => ['perk', 'wallbuy', 'door', 'debris', 'mysterybox', 'powerswitch', 'pap'].includes(e.kind))) {
    const blocker = ['door', 'debris'].includes(e.kind), p = center(e);
    if (blocker) p[2] = e.mins[2];
    const stand = offset(p, forward({ ...e, yaw: e.yaw ?? (e.kind === 'mysterybox' ? 270 : 0) }), blocker ? -48 : 40);
    const floor = floorAt(stand, e.kind === 'wallbuy' ? 96 : 18);
    if (floor) stand[2] = floor.maxs[2];
    if (!near(stand).some(n => reached.has(n.i))) add('purchase-unreachable', e, 'origin', `Purchase approach ${JSON.stringify(stand)} has no estimated spawn route with player/zombie clearance (doors treated open).`, `Move the item/yaw so its front is on connected floor with 32 units width and 72 units headroom; clear solids or add nodes along the route.`, 'FAIL', stand);
  }
  // Purchase use triggers as the kit spawns them (wall buy: 64-cube 24 in front of the model, cm_mapkit.cpp MkE_WallBuyBox;
  // perk: radius-44 cylinder 12 in front of the machine, z +48..+118, _mapkit.gsc mapkit_place_perk; box: 80x80x64).
  // Intersecting volumes send the use press to the wrong purchase (mapmaker-2 D4/D5: AK74u beside Juggernog).
  const trig = purchaseTriggers(edits);
  for (let x = 0; x < trig.length; x++) for (let y = x + 1; y < trig.length; y++) {
    const gap = triggerGap(trig[x], trig[y]), [a, b] = [trig[x], trig[y]];
    if (gap < 0) add('purchase-overlap', b.e, 'origin', `${b.e.kind} ${editName(b.e, b.e.i)} use trigger intersects ${editPath(a.e.i)} ${a.e.kind} ${editName(a.e, a.e.i)} by ${(-gap).toFixed(0)} units; a use press there can buy the wrong one.`, `Move one of them at least ${Math.ceil(-gap)} units further apart (horizontal trigger gap must be >= 0; wall buy beside a perk: 100+ units between origins).`);
  }
  return { findings, windows, zones, nodes, boxes, openings, lowestFloor: rooms.length ? Math.min(...rooms.map(r => r.mins[2])) : 0,
    assumptions: 'Offline links use standing 15/16-unit half-width, 72-unit headroom, <=18-unit step, <=192-unit link distance and floor samples every 8 units. Dynamic doors treated open; live engine graph is authoritative.' };
}
