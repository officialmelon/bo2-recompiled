// Decodes a raw tiled Xenos format-2 (8-bit) texture payload to a grayscale PNG
// using the SAME XenosTiledOffset2D swizzle as the native renderer, to check
// whether detiling scrambles the source (beaded strokes) or is correct.
// Usage: node scripts/shaders/decode-tiled-texture.mjs <payload.bin> <w> <h> <pitchField> <out.png>
import fs from 'fs';
import zlib from 'zlib';

const [, , src, wArg, hArg, pitchArg, out] = process.argv;
const width = Number(wArg), height = Number(hArg);
const pitchField = Number(pitchArg);
const data = fs.readFileSync(src);

const alignUp = (v, a) => (v + a - 1) & ~(a - 1);
function tiledOffset2D(x, y, pitch, bpbLog2) {
  pitch = alignUp(pitch, 32);
  const macro = (((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpbLog2 + 7)) >>> 0;
  const micro = (((x & 7) + ((y & 0xE) << 2)) << bpbLog2) >>> 0;
  const offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
          (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F)) >>> 0;
}

const pitchTexels = pitchField !== 0 ? pitchField << 5 : width;
// Grayscale image (coverage value).
const gray = Buffer.alloc(width * height);
for (let y = 0; y < height; y++) {
  for (let x = 0; x < width; x++) {
    const off = tiledOffset2D(x, y, pitchTexels, 0);
    gray[y * width + x] = off < data.length ? data[off] : 0;
  }
}

// Minimal PNG writer (grayscale 8-bit).
function crc32(buf) {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return (~c) >>> 0;
}
function chunk(type, body) {
  const len = Buffer.alloc(4); len.writeUInt32BE(body.length, 0);
  const tb = Buffer.concat([Buffer.from(type), body]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(tb), 0);
  return Buffer.concat([len, tb, crc]);
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
ihdr[8] = 8; ihdr[9] = 0; // 8-bit grayscale
const raw = Buffer.alloc((width + 1) * height);
for (let y = 0; y < height; y++) {
  raw[y * (width + 1)] = 0;
  gray.copy(raw, y * (width + 1) + 1, y * width, y * width + width);
}
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
  chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
]);
fs.writeFileSync(out, png);
console.log(`wrote ${out} (${width}x${height})`);
