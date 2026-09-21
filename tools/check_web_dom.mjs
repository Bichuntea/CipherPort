import { readFile } from "node:fs/promises";
import { webcrypto } from "node:crypto";
import { gunzipSync } from "node:zlib";
import { runInNewContext } from "node:vm";

const html = await readFile("web-manager/dist/index.html", "utf8");
const script = await readFile("web-manager/dist/app.js", "utf8");
const deviceApi = await readFile("web-manager/dist/device-api.js", "utf8");
const firmwareHtml = await readFile("web-manager/firmware/index.html");
const embeddedHtml = gunzipSync(await readFile("main/web_assets/index.html.gz"));
const ids = new Set([...html.matchAll(/id="([^"]+)"/g)].map((match) => match[1]));
const references = [...script.matchAll(/\$\("#([^"]+)"\)/g)].map((match) => match[1]);
const runtimeIds = new Set([
  "revealPassword", "copyPassword", "editRecord", "deleteRecord", "passwordValue",
  "wifiPassword", "wifiReveal",
]);
const missing = [...new Set(references.filter((id) => !ids.has(id) && !runtimeIds.has(id)))];
if (missing.length > 0) throw new Error(`missing DOM ids: ${missing.join(", ")}`);
if (!html.includes("简体中文") || !script.includes("Language saved and synchronized to the device")) {
  throw new Error("Web language selector or English translation is missing");
}
const staticTranslations = script.slice(script.indexOf("const STATIC_EN ="), script.indexOf("const ATTRIBUTE_EN ="));
const attributeTranslations = script.slice(script.indexOf("const ATTRIBUTE_EN ="), script.indexOf("const textBindings ="));
const translatedText = new Set([...staticTranslations.matchAll(/"([^"]+)":/g)].map((match) => match[1]));
const translatedAttributes = new Set([...attributeTranslations.matchAll(/"([^"]+)":/g)].map((match) => match[1]));
const visibleChinese = [...html.matchAll(/>([^<>]*[\u3400-\u9fff][^<>]*)</g)]
  .map((match) => match[1].trim()).filter(Boolean);
const missingText = [...new Set(visibleChinese.filter((value) => !translatedText.has(value)))];
if (missingText.length) throw new Error(`untranslated Web text: ${missingText.join(", ")}`);
const chineseAttributes = [...html.matchAll(/(?:placeholder|aria-label|title)="([^"]*[\u3400-\u9fff][^"]*)"/g)]
  .map((match) => match[1]);
const missingAttributes = [...new Set(chineseAttributes.filter((value) => !translatedAttributes.has(value)))];
if (missingAttributes.length) throw new Error(`untranslated Web attributes: ${missingAttributes.join(", ")}`);
if (!deviceApi.includes('getLanguage: () => request("/api/settings/language")') ||
    !deviceApi.includes('updateLanguage: (language) => request("/api/settings/language"')) {
  throw new Error("device language API bindings are missing");
}
if (ids.has("syncBrowserTime") || ids.has("customDeviceTime") || ids.has("setCustomDeviceTime") ||
    deviceApi.includes('request("/api/time")') || script.includes("formatDeviceClock")) {
  throw new Error("removed Web clock synchronization controls or API bindings remain");
}
if (!script.includes("const saved = await deviceApi.getWelcome()")) {
  throw new Error("welcome UTF-8 save verification is missing");
}
const passwordField = { value: "" };
const strengthField = { textContent: "" };
const sandbox = {
  window: {}, crypto: webcrypto,
  localStorage: { getItem: () => "en" },
  document: {
    readyState: "loading", addEventListener: () => {},
    querySelector: (selector) => selector === "#passwordInput" ? passwordField :
      selector === "#passwordStrength" ? strengthField : null,
  },
};
runInNewContext(script, sandbox);
for (let index = 0; index < 100; index += 1) {
  sandbox.generatePassword();
  if (passwordField.value.length !== 12 ||
      !/[A-Z]/.test(passwordField.value) || !/[a-z]/.test(passwordField.value) ||
      !/[0-9]/.test(passwordField.value) || !/[!@#$%]/.test(passwordField.value)) {
    throw new Error("Web record generator did not produce 12 mixed characters");
  }
}
if (!firmwareHtml.equals(embeddedHtml)) {
  throw new Error("embedded firmware Web asset is stale; run tools/build_web_firmware.mjs");
}
for (const [index, match] of [...embeddedHtml.toString("utf8").matchAll(/<script>([\s\S]*?)<\/script>/g)].entries()) {
  try {
    new Function(match[1]);
  } catch (error) {
    throw new Error(`embedded firmware script ${index + 1} is invalid: ${error.message}`);
  }
}
console.log(`Web DOM bindings: PASS (${ids.size} ids, ${new Set(references).size} referenced)`);
console.log(`Embedded Web asset: PASS (${embeddedHtml.length} bytes)`);
