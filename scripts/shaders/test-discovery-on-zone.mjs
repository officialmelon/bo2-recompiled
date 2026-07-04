// Ground-truth test: run discoverUcode on a zone window ending at a known
// runtime payload location; the scan should pick the payload start exactly.
// Usage: node scripts/shaders/test-discovery-on-zone.mjs <zone.dat> <payloadOffset> <lead>
import fs from 'fs';
import path from 'path';
import pkg from 'xxhash-addon';
import { discoverUcode } from './scan-containers-ucode-discovery.mjs';
const { XXHash3 } = pkg;

const [, , zone, offStr, leadStr] = process.argv;
const payloadOffset = Number(offStr);
const lead = Number(leadStr || 256);
const fd = fs.openSync(path.join('shader_work/fastfile_dat', zone), 'r');
const buf = Buffer.alloc(lead + 512 * 1024);
fs.readSync(fd, buf, 0, buf.length, payloadOffset - lead);
fs.closeSync(fd);
const found = discoverUcode(buf);
if (!found) { console.log('no candidate found'); process.exit(1); }
console.log(`candidate startByte=${found.startByte} (expected ${lead}) dwords=${found.extentDwords}`);
const slice = buf.subarray(found.startByte, found.startByte + found.extentDwords * 4);
const h = new XXHash3(Buffer.alloc(8));
h.update(slice);
console.log(`hash=0x${h.digest().readBigUInt64BE(0).toString(16).toUpperCase().padStart(16, '0')}`);
