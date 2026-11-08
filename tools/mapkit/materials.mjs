// mapkit: list the model materials (mc/...) a base map's fastfile carries, the names a layout may use for
// floorMaterial / wallMaterial / ceilingMaterial / box material. Reads the retail zone (read only).
//   node tools/mapkit/materials.mjs [zombie_pentagon] [filter-regex]
// Names ending _dark are dark textures (the mapkit_test walls drew near black with mc/pent_art_wall_wood07_dark, run mk17).
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { zoneRoot as defaultZoneRoot } from './zone-root.mjs';

const map = process.argv[2] || 'zombie_pentagon';
const filter = new RegExp(process.argv[3] || '.', 'i');
// the install's zone folder (see zone-root.mjs)
const zoneRoot = defaultZoneRoot();
let file = null;
for (const dir of fs.readdirSync(zoneRoot)) {
    const p = path.join(zoneRoot, dir, map + '.ff');
    if (fs.existsSync(p)) { file = p; break; }
}
if (!file) { console.error(`no ${map}.ff under ${zoneRoot}`); process.exit(1); }
const data = fs.readFileSync(file);
let out = null;
for (let off = 8; off < 64 && !out; ++off) {
    if (data[off] !== 0x78) continue;
    try { out = zlib.inflateSync(data.subarray(off), { finishFlush: zlib.constants.Z_SYNC_FLUSH }); } catch { }
}
if (!out) { console.error('could not inflate ' + file); process.exit(1); }
const names = new Set(out.toString('latin1').match(/mc\/[a-z0-9_]+/g) || []);
const list = [...names].filter(n => filter.test(n)).sort();
console.log(`${file}: ${names.size} mc/ materials, ${list.length} match`);
console.log(list.join('\n'));
