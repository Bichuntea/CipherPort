import { createGzip } from "node:zlib";
import { createReadStream, createWriteStream } from "node:fs";
import { pipeline } from "node:stream/promises";

const [input, output] = process.argv.slice(2);
if (!input || !output) {
  throw new Error("usage: node tools/pack_web_assets.mjs INPUT OUTPUT");
}
await pipeline(createReadStream(input), createGzip({ level: 9 }), createWriteStream(output));
