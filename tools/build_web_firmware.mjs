import { readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { gzipSync } from "node:zlib";

const root = resolve(import.meta.dirname, "..");
const dist = resolve(root, "web-manager", "dist");
const output = resolve(root, "web-manager", "firmware", "index.html");
const embeddedOutput = resolve(root, "main", "web_assets", "index.html.gz");

let html = await readFile(resolve(dist, "index.html"), "utf8");
const css = await readFile(resolve(dist, "styles.css"), "utf8");
let api = await readFile(resolve(dist, "device-api.js"), "utf8");
let app = await readFile(resolve(dist, "app.js"), "utf8");
const favicon = await readFile(resolve(dist, "favicon.svg"));
const faviconData = `data:image/svg+xml;base64,${favicon.toString("base64")}`;
const bootstrap = `<script>(function(){
  window.__cipherportReady=false;
  function report(message){
    var old=document.getElementById("cipherport-startup-error");if(old)return;
    var item=document.createElement("div");item.id="cipherport-startup-error";
    item.style.cssText="position:fixed;left:12px;right:12px;bottom:12px;z-index:9999;padding:12px;background:#7b1d1d;color:white;font:14px sans-serif";
    item.textContent=message;(document.body||document.documentElement).appendChild(item);
  }
  window.addEventListener("error",function(event){if(!window.__cipherportReady&&event&&event.message){report("Web Manager failed to start: "+event.message+". Reload this page.");}});
  try{var request=new XMLHttpRequest();request.open("POST","/api/session/connect",true);request.send(null);}catch(error){}
  window.addEventListener("load",function(){setTimeout(function(){if(!window.__cipherportReady)report("Web Manager failed to start. Reload this page.");},10000);});
})();</script>`;

// Preview fixtures must never ship inside the device firmware.
app = app.replace(
  /const records = deviceMode \? \[\] : \[[\s\S]*?\n\];/,
  "const records = [];",
);

// The embedded page is always served by the device. Do not let captive-portal
// host aliases or /index.html navigation accidentally select preview mode.
api = api.replace(
  'isDeviceHost: () => location.hostname === "192.168.4.1"',
  "isDeviceHost: () => true",
);

html = html
  .replace("</head>", () => `${bootstrap}\n</head>`)
  .replace(/<link rel="icon"[^>]*>/, () => `<link rel="icon" href="${faviconData}" type="image/svg+xml" />`)
  .replace(/<link rel="stylesheet"[^>]*>/, () => `<style>${css}</style>`)
  .replace(/\s*<script src="\.\/device-api\.js[^>]*><\/script>/, () => `\n<script>${api}</script>`)
  .replace(/\s*<script src="\.\/app\.js[^>]*><\/script>/, () => `\n<script>${app}</script>`);

if (/DEMO-PASSWORD|<script src=|<link rel="stylesheet"/.test(html)) {
  throw new Error("firmware Web bundle still contains preview-only or external assets");
}
for (const [index, match] of [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].entries()) {
  try {
    new Function(match[1]);
  } catch (error) {
    throw new Error(`firmware inline script ${index + 1} is invalid: ${error.message}`);
  }
}
await writeFile(output, html, "utf8");
await writeFile(embeddedOutput, gzipSync(Buffer.from(html, "utf8"), { level: 9 }));
