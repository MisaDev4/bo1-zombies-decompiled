// The retail zone folder (the .ff fastfiles) the mapkit tools read, read only: build/Release/zone when it exists (a
// copied install), otherwise <install>/zone, the install being the folder setup.ps1 records in gamedir.txt.
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

export function zoneRoot() {
  const repo = fileURLToPath(new URL('../../', import.meta.url));
  const local = repo + 'build/Release/zone';
  if (fs.existsSync(local)) return local;
  try {
    const install = fs.readFileSync(repo + 'gamedir.txt', 'utf8').split(/\r?\n/)[0].trim();
    if (install) return install + '/zone';
  } catch (e) { /* no gamedir.txt: setup.ps1 has not run */ }
  return local;
}
