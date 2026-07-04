// Probes whether the shader container header fields identify the exact
// runtime microcode program range: parses the XenosRecomp-documented
// ShaderContainer/Shader structs and checks XXH3(big-endian slice) against a
// known runtime hash.
//
// Usage: node scripts/shaders/probe-container-program-range.mjs <container.bin> <runtime_hash_hex>
import fs from 'fs';
import pkg from 'xxhash-addon';
const { XXHash3 } = pkg;

const [, , containerPath, runtimeHashHex] = process.argv;
if (!containerPath || !runtimeHashHex) {
  console.error('usage: probe-container-program-range.mjs <container.bin> <runtime_hash_hex>');
  process.exit(2);
}
const target = BigInt(runtimeHashHex);
const data = fs.readFileSync(containerPath);
const be32 = (offset) => data.readUInt32BE(offset);

const flags = be32(0);
const virtualSize = be32(4);
const physicalSize = be32(8);
const constantTableOffset = be32(16);
const definitionTableOffset = be32(20);
const shaderOffset = be32(24);
console.log(`flags=0x${flags.toString(16)} virtual=${virtualSize} physical=${physicalSize}`);
console.log(`constantTable=0x${constantTableOffset.toString(16)} definitionTable=0x${definitionTableOffset.toString(16)} shader=0x${shaderOffset.toString(16)}`);

const programPhysicalOffset = be32(shaderOffset);
const programSize = be32(shaderOffset + 4);
console.log(`Shader.physicalOffset=0x${programPhysicalOffset.toString(16)} Shader.size=${programSize}`);

const hashSlice = (begin, size, label) => {
  if (begin < 0 || begin + size > data.length || size === 0) {
    console.log(`  ${label}: out of range (begin=${begin} size=${size} file=${data.length})`);
    return false;
  }
  const slice = data.subarray(begin, begin + size);
  const hasher = new XXHash3(Buffer.alloc(8));
  hasher.update(slice);
  const digest = hasher.digest();
  const value = digest.readBigUInt64BE ? digest.readBigUInt64BE(0) : BigInt('0x' + digest.toString('hex'));
  const match = value === target;
  console.log(`  ${label}: begin=${begin} size=${size} xxh3=0x${value.toString(16).toUpperCase().padStart(16, '0')} match=${match}`);
  return match;
};

console.log('candidate program ranges:');
// The physical (microcode) section follows the virtual section.
hashSlice(virtualSize + programPhysicalOffset, programSize, 'virtual+physOffset');
hashSlice(programPhysicalOffset, programSize, 'physOffset direct');
hashSlice(virtualSize, programSize, 'virtual base');
