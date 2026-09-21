const deviceApi = window.CipherPortDeviceApi;
const deviceMode = Boolean(deviceApi && deviceApi.isDeviceHost && deviceApi.isDeviceHost());
const records = deviceMode ? [] : [
  { id: 1, name: "代码平台", note: "开发账号", username: "demo-user-01", password: "DEMO-PASSWORD-01" },
  { id: 2, name: "云端账号", note: "个人云服务", username: "demo-user-02", password: "DEMO-PASSWORD-02" },
  { id: 3, name: "知识库", note: "团队资料", username: "demo-team", password: "DEMO-PASSWORD-03" },
  { id: 4, name: "云服务", note: "管理控制台", username: "demo-admin", password: "DEMO-PASSWORD-04" },
  { id: 5, name: "金融示例", note: "演示记录", username: "demo-user-05", password: "DEMO-PASSWORD-05" },
  { id: 6, name: "设计平台", note: "设计协作", username: "demo-design", password: "DEMO-PASSWORD-06" },
];

let selectedId = deviceMode ? 0 : 1;
let approvalTimer = null;
let mutationPoll = null;
let sessionSeconds = 300;
let sessionEnded = false;
let revealTimer = null;
let pendingAction = null;
let webLanguage = localStorage.getItem("cipherport-language") === "en" ? "en" : "zh-CN";

const STATIC_EN = Object.freeze({
  "锁定并断开": "Lock and disconnect", "立即结束管理会话": "End management session now",
  "浏览器不会保存密码库": "The browser does not store the vault",
  "修改会暂存在设备内存中，并等待你在设备上按 OK 后写入。": "Changes wait in device memory until you press OK on the device.",
  "换一个关键词或清除筛选。": "Try another keyword or clear the filter.",
  "涉及连接或数据的变更仍需设备端确认。": "Connection and data changes still require device confirmation.",
  "管理网络": "Management network", "运行中": "Running",
  "15 秒": "15 seconds", "30 秒": "30 seconds", "1 分钟": "1 minute", "2 分钟": "2 minutes", "5 分钟": "5 minutes",
  "简体中文": "Simplified Chinese", "新增密码记录": "Add password record", "输入或生成一个强密码": "Enter or generate a strong password",
  "确认写入": "Confirm write", "新增 GitHub": "Add GitHub", "短按 OK 确认": "Press OK to confirm",
  "网页提交的内容正在设备内存中等待确认。超时或断开连接后将自动丢弃。": "The submitted data awaits confirmation in device memory and is discarded on timeout or disconnect.",
  "草稿不会写入浏览器": "Draft is not stored in the browser", "原型演示：模拟设备 OK": "Prototype demo: simulate device OK",
  "密码库": "Vault", "设备设置": "Device Settings", "安全会话已连接": "Secure session connected",
  "仅限当前设备": "Current device only", "会话剩余": "Session remaining", "+5 分钟": "+5 min",
  "记录仅保存在这台 AI Passport 上。": "Records stay on this AI Passport.", "新增记录": "Add Record",
  "敏感操作会保留在设备内": "Sensitive operations stay on device",
  "修改或删除先暂存在设备内存中，并等待你在设备上按 OK 后写入。": "Changes wait in device memory until you press OK on the device.",
  "没有匹配的记录": "No matching records", "换一个关键词或清空筛选。": "Try another keyword or clear the filter.",
  "设备配置": "Device Configuration", "涉及连接和数据的变更需要设备上确认。": "Connection and data changes require device confirmation.",
  "网络管理": "Network Management", "临时 SoftAP，仅允许一台客户端": "Temporary SoftAP; one client only", "已连接": "Connected",
  "网络名称": "Network name", "管理地址": "Management address", "设备屏幕显示": "Shown on device", "复制": "Copy",
  "Wi-Fi 密码策略": "Wi-Fi password policy", "每次进入管理模式生成新的 8 位数字密码": "A new 8-digit password is generated for each management session",
  "显示与安全": "Display & Security", "敏感内容的可见时间": "Sensitive content visibility", "密码显示时长": "Password reveal time",
  "全按键提示音": "Key sounds", "UP、DOWN、OK 的每次按键都会提示": "Sound on every UP, DOWN and OK press",
  "自动锁定时间": "Auto-lock time", "保存显示设置": "Save Display Settings", "设备首页": "Device Home",
  "管理设备欢迎页显示的内容": "Manage the message shown on the device home screen", "首页标题": "Home title", "首页说明": "Home subtitle",
  "保存首页内容": "Save Home Content", "界面语言": "Interface Language", "网页和设备使用同一种语言。": "The web manager and device use the same language.",
  "语言": "Language", "保存语言": "Save Language", "安全操作": "Security Actions", "PIN 与设备数据管理": "PIN and device data management",
  "在设备上修改 PIN": "Change PIN on Device", "PIN 始终在设备上输入，浏览器不会读取或保存。": "PIN entry stays on the device; the browser never reads or stores it.",
  "清除设备全部数据": "Clear All Device Data", "清除密码库和设置，需要在设备上再次确认。": "Clears the vault and settings after device confirmation.",
  "新建密码记录": "New Password Record", "名称": "Name", "备注": "Note", "用户名": "Username", "密码": "Password", "生成": "Generate",
  "取消": "Cancel", "发送到设备确认": "Send for Device Approval", "等待实体确认": "Waiting for physical approval",
  "请在设备上按 OK": "Press OK on the device", "网页提交后会保留在设备内存中等待确认。超时或断开连接后自动清除。": "The request waits in device memory and is cleared on timeout or disconnect.",
  "草稿不会写入密码库": "Draft is not written to the vault", "取消本次操作": "Cancel Operation", "保险库为空": "Vault is empty",
  "新增记录后将在这里显示详细信息。": "Add a record to see its details here.", "新增记录后会在这里显示详细信息。": "Add a record to see its details here.",
  "暂无备注": "No note", "密码 · 显示后 15 秒自动隐藏": "Password · hides 15 seconds after reveal",
  "显示": "Reveal", "编辑记录": "Edit Record", "删除": "Delete", "关闭": "Close"
});

const ATTRIBUTE_EN = Object.freeze({
  "主导航": "Main navigation", "CipherPort Admin 首页": "CipherPort Admin home",
  "关闭导航": "Close navigation", "打开导航": "Open navigation",
  "点击延长 5 分钟": "Click to extend by 5 minutes", "延长管理会话 5 分钟": "Extend management session by 5 minutes",
  "连接与安全说明": "Connection and security help", "关闭提示": "Close message",
  "密码记录": "Password records", "搜索名称、备注或用户名": "Search name, note, or username",
  "记录详情": "Record details", "全按键提示音": "Key sounds", "设备首页预览": "Device home preview",
  "关闭": "Close", "例如 GitHub": "For example, GitHub", "可选说明": "Optional note"
});

const textBindings = new WeakMap();
const attributeBindings = new WeakMap();
function translateSubtree(root = document.body) {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const raw = node.nodeValue || "";
    const trimmed = raw.trim();
    let binding = textBindings.get(node);
    if (!binding && STATIC_EN[trimmed]) {
      const start = raw.indexOf(trimmed);
      binding = { zh: trimmed, en: STATIC_EN[trimmed], prefix: raw.slice(0, start), suffix: raw.slice(start + trimmed.length) };
      textBindings.set(node, binding);
    }
    if (binding) node.nodeValue = binding.prefix + (webLanguage === "en" ? binding.en : binding.zh) + binding.suffix;
  }
  for (const element of root.querySelectorAll("[placeholder], [aria-label], [title]")) {
    let bindings = attributeBindings.get(element);
    if (!bindings) { bindings = {}; attributeBindings.set(element, bindings); }
    for (const attribute of ["placeholder", "aria-label", "title"]) {
      const value = element.getAttribute(attribute);
      if (!bindings[attribute] && ATTRIBUTE_EN[value]) bindings[attribute] = { zh: value, en: ATTRIBUTE_EN[value] };
      if (bindings[attribute]) element.setAttribute(attribute, webLanguage === "en" ? bindings[attribute].en : bindings[attribute].zh);
    }
  }
  document.documentElement.lang = webLanguage === "en" ? "en" : "zh-CN";
  const languageSelect = document.querySelector("#webLanguageSelect");
  if (languageSelect) languageSelect.value = webLanguage;
}

function setWebLanguage(language, persist = true) {
  webLanguage = language === "en" ? "en" : "zh-CN";
  if (persist) localStorage.setItem("cipherport-language", webLanguage);
  translateSubtree();
  if (document.querySelector("#recordList")) renderRecords();
  const revealOutput = document.querySelector("#revealOutput");
  const revealRange = document.querySelector("#revealRange");
  if (revealOutput && revealRange) revealOutput.textContent = `${revealRange.value} ${webLanguage === "en" ? "sec" : "秒"}`;
}

const tr = (zh, en) => webLanguage === "en" ? en : zh;

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => Array.prototype.slice.call(root.querySelectorAll(selector));
const escapeHtml = (value = "") => String(value).replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[char]);

function filteredRecords() {
  const term = $("#searchInput").value.trim().toLowerCase();
  return records.filter((record) => {
    const searchMatch = !term || [record.name, record.note, record.username].some((field) => field.toLowerCase().includes(term));
    return searchMatch;
  });
}

function renderRecords() {
  const matches = filteredRecords();
  const filtering = $("#searchInput").value.trim().length > 0;
  $("#recordCount").textContent = `${records.length} ${records.length === 1 ? "PASSWORD" : "PASSWORDS"}`;
  $("#recordList").innerHTML = matches.map((record) => `
    <div class="record-item ${record.id === selectedId ? "is-active" : ""}">
      <button class="record-main" data-record-id="${record.id}">
        <span class="record-text"><strong>${escapeHtml(record.name)}</strong><span>${escapeHtml(record.note || "暂无备注")}</span></span>
      </button>
      <span class="reorder-controls" aria-label="${tr(`调整 ${escapeHtml(record.name)} 的设备显示顺序`, `Change display order of ${escapeHtml(record.name)}`)}">
        <button data-move="-1" data-move-id="${record.id}" aria-label="${tr(`上移 ${escapeHtml(record.name)}`, `Move ${escapeHtml(record.name)} up`)}" ${filtering || records.indexOf(record) === 0 ? "disabled" : ""}>↑</button>
        <button data-move="1" data-move-id="${record.id}" aria-label="${tr(`下移 ${escapeHtml(record.name)}`, `Move ${escapeHtml(record.name)} down`)}" ${filtering || records.indexOf(record) === records.length - 1 ? "disabled" : ""}>↓</button>
      </span>
    </div>`).join("");
  $("#emptyState").hidden = matches.length > 0;
  $$("[data-record-id]").forEach((button) => button.addEventListener("click", () => selectRecord(Number(button.dataset.recordId))));
  $$("[data-move-id]").forEach((button) => button.addEventListener("click", () => moveRecord(Number(button.dataset.moveId), Number(button.dataset.move))));
  translateSubtree($("#view-vault"));
}

function renderDetail() {
  const record = records.find((item) => item.id === selectedId) || records[0];
  if (!record) {
    $("#detailPanel").innerHTML = `<div class="empty-detail"><strong>密码库为空</strong><p>新增记录后会在这里显示详细信息。</p></div>`;
    translateSubtree($("#detailPanel"));
    return;
  }
  $("#detailPanel").innerHTML = `
    <div class="detail-header">
      <div><h2>${escapeHtml(record.name)}</h2><p class="record-note">${escapeHtml(record.note || "暂无备注")}</p></div>
    </div>
    <div class="detail-section">
      <div class="section-label"><span>用户名</span><button data-copy-value="${escapeHtml(record.username)}">复制</button></div>
      <div class="value-box"><code>${escapeHtml(record.username)}</code></div>
    </div>
    <div class="detail-section">
      <div class="section-label"><span>密码 · 显示后 15 秒自动隐藏</span><button id="revealPassword">显示</button></div>
      <div class="value-box"><code id="passwordValue">••••••••••••••••</code><button id="copyPassword" disabled>复制</button></div>
    </div>
    <div class="detail-actions"><button class="secondary-button" id="editRecord">编辑记录</button><button class="text-button" id="deleteRecord">删除</button></div>`;

  $("#revealPassword").addEventListener("click", () => revealPassword(record));
  $("#copyPassword").addEventListener("click", () => copyText(record.password, tr("密码已复制，剪贴板内容请及时清除", "Password copied; clear the clipboard promptly")));
  $("[data-copy-value]").addEventListener("click", (event) => copyText(event.currentTarget.dataset.copyValue, tr("用户名已复制", "Username copied")));
  $("#editRecord").addEventListener("click", () => openEditor(record));
  $("#deleteRecord").addEventListener("click", () => {
    const index = deviceMode ? record.index
      : records.findIndex((item) => item.id === record.id);
    if (deviceMode) {
      void stageDeviceMutation(`${tr("删除", "Delete")} ${record.name}`,
        tr("请在设备上选择 APPROVE 或 CANCEL，然后短按 OK。", "Select APPROVE or CANCEL on the device, then press OK."),
        { operation: "delete", index });
    } else {
      stageAction(`删除 ${record.name}`, "删除后仍需设备端按 OK，写入成功前旧记录保持有效。", () => {
        records.splice(index, 1); selectedId = records[0] ? records[0].id : 0; renderRecords(); renderDetail();
      });
    }
  });
  translateSubtree($("#detailPanel"));
}

function moveRecord(id, direction) {
  const from = records.findIndex((record) => record.id === id);
  const to = from + direction;
  if (from < 0 || to < 0 || to >= records.length) return;
  const record = records[from];
  selectedId = id;
  stageAction(`调整 ${record.name} 的顺序`, `将在设备密码库中把这条记录${direction < 0 ? "上移" : "下移"}一位。短按设备 OK 后写入。`, () => {
    const [moved] = records.splice(from, 1);
    records.splice(to, 0, moved);
    renderRecords();
    renderDetail();
  });
}

async function loadDeviceRecords() {
  const fresh = await deviceApi.listRecords();
  records.splice(0, records.length, ...fresh);
  if (!records.some((record) => record.id === selectedId)) selectedId = records[0] ? records[0].id : 0;
  renderRecords();
  renderDetail();
}

async function stageDeviceMutation(title, description, payload) {
  pendingAction = null;
  $("#approvalDeviceName").textContent = title;
  $("#approvalDescription").textContent = description;
  $("#approvalModal").hidden = false;
  $("#demoApprove").hidden = true;
  $("#cancelApproval").textContent = tr("隐藏提示", "Hide prompt");
  try {
    await deviceApi.stageMutation(payload);
  } catch (_) {
    $("#approvalModal").hidden = true;
  toast(tr("请求发送失败，请先完成设备上的待处理操作", "Request failed. Finish the pending operation on the device first."));
    return;
  }
  clearInterval(mutationPoll);
  mutationPoll = setInterval(async () => {
    try {
      const result = await deviceApi.getMutationStatus();
      if (result.status === "pending" || result.status === "none") return;
      clearInterval(mutationPoll);
      $("#approvalModal").hidden = true;
      if (result.status === "approved") {
        await loadDeviceRecords();
        toast(tr("设备已确认并安全写入", "Device approved and saved securely"));
      } else if (result.status === "denied") {
        toast(tr("设备已取消本次操作", "Device canceled this operation"));
      } else {
        toast(tr("写入失败，原数据保持不变", "Write failed; original data is unchanged"));
      }
    } catch (_) {
      $("#approvalDescription").textContent = tr("连接暂时中断，正在自动重连。请保持此页面打开。", "Connection interrupted. Reconnecting; keep this page open.");
    }
  }, 700);
}

async function connectDevice() {
  try {
    const language = await deviceApi.getLanguage();
    setWebLanguage(language.language);
  } catch (_) {}
  const managementAddress = window.location.origin;
  $("#ipInput").value = managementAddress;
  const addressCopy = $("[data-copy]");
  if (addressCopy) addressCopy.dataset.copy = managementAddress;
  $("#approvalDeviceName").textContent = tr("授权 Web Management", "Authorize Web Management");
  $("#approvalDescription").textContent = tr("请在设备上短按 OK。此页面会在授权后自动载入密码库。", "Press OK on the device. The vault loads automatically after authorization.");
  $("#approvalModal").hidden = false;
  $("#demoApprove").hidden = true;
  $("#cancelApproval").hidden = true;
  try { await deviceApi.connect(); } catch (_) {}
  for (let attempt = 0; attempt < 150; attempt += 1) {
    try {
      const session = await deviceApi.getSession();
      if (session.status === "authorized") {
        $("#approvalModal").hidden = true;
        $("#cancelApproval").hidden = false;
        await loadDeviceRecords();
        try {
          const language = await deviceApi.getLanguage();
          setWebLanguage(language.language);
        } catch (_) {}
        try {
          const welcome = await deviceApi.getWelcome();
          const lines = String(welcome.message || "").split("\n");
          $("#homeTitle").value = lines.shift() || "PASSWORD MANAGER";
          $("#homeSubtitle").value = lines.join(" ") || "YOUR KEYS. YOUR CONTROL.";
        } catch (_) {}
        try {
          const display = await deviceApi.getDisplaySettings();
          $("#autoLockSelect").value = String(display.autoLockSeconds || 120);
          $("#revealRange").value = String(display.revealSeconds || 15);
          $("#revealOutput").textContent = `${$("#revealRange").value} 秒`;
          const keySound = $("#keySoundSwitch");
          keySound.classList.toggle("is-on", Boolean(display.keySoundEnabled));
          keySound.setAttribute("aria-checked", String(Boolean(display.keySoundEnabled)));
        } catch (_) {}
        return;
      }
    } catch (_) {}
    await new Promise((resolve) => setTimeout(resolve, 800));
  }
          $("#approvalDescription").textContent = tr("授权等待已超时，请退出后重新进入 Web Management。", "Authorization timed out. Exit and reopen Web Management.");
}

function selectRecord(id) {
  selectedId = id;
  renderRecords();
  renderDetail();
  if (window.innerWidth <= 760) $("#detailPanel").classList.add("mobile-visible");
}

function revealPassword(record) {
  const value = $("#passwordValue");
  const button = $("#revealPassword");
  const copy = $("#copyPassword");
  clearTimeout(revealTimer);
  value.textContent = record.password;
  copy.disabled = false;
  let left = 15;
  button.textContent = tr(`隐藏 ${left}s`, `Hide ${left}s`);
  const interval = setInterval(() => { left -= 1; button.textContent = left > 0 ? tr(`隐藏 ${left}s`, `Hide ${left}s`) : tr("显示", "Reveal"); }, 1000);
  revealTimer = setTimeout(() => { clearInterval(interval); value.textContent = "••••••••••••••••"; copy.disabled = true; button.textContent = tr("显示", "Reveal"); }, 15000);
  button.onclick = () => { clearInterval(interval); clearTimeout(revealTimer); renderDetail(); };
}

function openEditor(record = null) {
  const form = $("#recordForm");
  form.reset();
  $("#editorTitle").textContent = record ? `${tr("编辑", "Edit")} ${record.name}` : tr("新增密码记录", "Add password record");
  form.dataset.editId = record ? record.id : "";
  if (record) {
    ["name", "note", "username", "password"].forEach((key) => { if (form.elements[key]) form.elements[key].value = record[key] || ""; });
  }
  $("#editorModal").hidden = false;
  setTimeout(() => form.elements.name.focus(), 20);
}

function closeEditor() { $("#editorModal").hidden = true; }

function generatePassword() {
  const groups = ["ABCDEFGHJKLMNPQRSTUVWXYZ", "abcdefghijkmnopqrstuvwxyz", "23456789", "!@#$%"];
  const alphabet = groups.join("");
  const sample = new Uint32Array(1);
  const draw = (limit) => {
    const cutoff = Math.floor(0x100000000 / limit) * limit;
    do { crypto.getRandomValues(sample); } while (sample[0] >= cutoff);
    return sample[0] % limit;
  };
  const characters = groups.map((group) => group[draw(group.length)]);
  while (characters.length < 12) characters.push(alphabet[draw(alphabet.length)]);
  for (let index = characters.length - 1; index > 0; index -= 1) {
    const other = draw(index + 1);
    [characters[index], characters[other]] = [characters[other], characters[index]];
  }
  $("#passwordInput").value = characters.join("");
  $("#passwordStrength").textContent = tr("12 位随机密码 · 包含大小写、数字和符号", "12-character random password · letters, numbers, and symbols");
}

function stageAction(title, description, apply) {
  pendingAction = apply;
  $("#approvalDeviceName").textContent = title;
  $("#approvalDescription").textContent = description;
  $("#approvalModal").hidden = false;
  let left = 60;
  clearInterval(approvalTimer);
  approvalTimer = setInterval(() => {
    left -= 1;
    $("#approvalClock").textContent = `00:${String(left).padStart(2, "0")}`;
    $("#approvalProgress").style.transform = `scaleX(${left / 60})`;
  if (left <= 0) cancelApproval(tr("确认已超时，内存草稿已丢弃", "Approval timed out; the in-memory draft was discarded"));
  }, 1000);
}

function cancelApproval(message = tr("本次操作已取消，未写入设备", "Operation canceled; nothing was saved to the device")) {
  clearInterval(approvalTimer);
  $("#approvalModal").hidden = true;
  pendingAction = null;
  toast(message);
}

function approveAction() {
  clearInterval(approvalTimer);
  if (pendingAction) pendingAction();
  pendingAction = null;
  $("#approvalModal").hidden = true;
  toast(tr("设备确认成功，已完成原子写入", "Device approved; write completed"));
}

function copyText(text, message) {
  if (!navigator.clipboard || !navigator.clipboard.writeText) return toast(tr("浏览器未允许访问剪贴板", "Browser clipboard access is unavailable"));
  navigator.clipboard.writeText(text).then(() => toast(message)).catch(() => toast(tr("浏览器未允许访问剪贴板", "Browser clipboard access is unavailable")));
}

function toast(message) {
  const item = document.createElement("div");
  item.className = "toast";
  item.textContent = message;
  $("#toastRegion").append(item);
  setTimeout(() => item.remove(), 3500);
}

function navigate(view) {
  $$(".nav-item").forEach((item) => item.classList.toggle("is-active", item.dataset.view === view));
  $$(".view").forEach((section) => section.classList.toggle("is-active", section.id === `view-${view}`));
  setMenuOpen(false);
}

function setMenuOpen(open) {
  $(".sidebar").classList.toggle("is-open", open);
  $("#menuButton").setAttribute("aria-expanded", String(open));
  $("#navScrim").hidden = !open;
  document.body.classList.toggle("menu-open", open);
}

function updateWifiPasswordMode() {
  const selected = $("input[name='wifiPasswordMode']:checked");
  if (!selected) return;
  const fixed = selected.value === "fixed";
  const input = $("#wifiPassword");
  const reveal = $("#wifiReveal");
  input.disabled = !fixed;
  reveal.disabled = !fixed;
  if (!fixed) {
    input.value = "";
    input.type = "password";
    reveal.textContent = "显示";
  }
}

function registerWebMcpTools() {
  const context = document.modelContext;
  if (!context || !context.registerTool) return;
  const tools = [
    {
      name: "search_vault_records", title: "搜索密码库记录", description: "按平台、网址或用户名筛选当前设备密码库列表，不返回密码。",
      inputSchema: { type: "object", properties: { query: { type: "string", maxLength: 80 } }, required: ["query"], additionalProperties: false },
      annotations: { readOnlyHint: true, untrustedContentHint: false },
      execute({ query }) { $("#searchInput").value = String(query); navigate("vault"); renderRecords(); return { visibleRecordNames: filteredRecords().map((record) => record.name), count: filteredRecords().length }; },
    },
    {
      name: "start_new_vault_record", title: "开始新增密码记录", description: "打开新增记录表单；不会保存或写入设备。",
      inputSchema: { type: "object", properties: {}, additionalProperties: false },
      annotations: { readOnlyHint: false, untrustedContentHint: false },
      execute() { navigate("vault"); openEditor(); return { status: "editor_open", persistentWrite: false, deviceApprovalRequired: true }; },
    },
  ];
  tools.forEach((tool) => { try { void Promise.resolve(context.registerTool(tool)).catch(() => {}); } catch (_) {} });
}

function initializeApp() {
  if (window.__cipherportReady) return;
  renderRecords(); renderDetail();
  $$(".nav-item").forEach((item) => item.addEventListener("click", () => navigate(item.dataset.view)));
  $("#searchInput").addEventListener("input", renderRecords);
  $("#dismissNote").addEventListener("click", () => $(".security-note").remove());
  $("#addButton").addEventListener("click", () => openEditor());
  $$(".modal-close").forEach((button) => button.addEventListener("click", closeEditor));
  $("#generatePassword").addEventListener("click", generatePassword);
  $("#recordForm").addEventListener("submit", (event) => {
    event.preventDefault();
    const form = event.currentTarget;
    const values = {};
    new FormData(form).forEach((value, key) => { values[key] = value; });
    const editId = Number(form.dataset.editId);
    closeEditor();
    if (deviceMode) {
      const existing = records.find((record) => record.id === editId);
      const index = deviceMode ? (existing ? existing.index : 0) : records.findIndex((record) => record.id === editId);
      void stageDeviceMutation(`${editId ? tr("更新", "Update") : tr("新增", "Add")} ${values.name}`,
        tr("请核对设备屏幕上的记录名称，再选择 APPROVE 或 CANCEL。", "Check the record name on the device, then select APPROVE or CANCEL."),
        { operation: editId ? "edit" : "add", index: Math.max(0, index),
          platform: values.name, note: values.note || "", url: "",
          username: values.username, password: values.password });
    } else {
      stageAction(`${editId ? "更新" : "新增"} ${values.name}`, "核对设备屏幕上的名称、用户名和密码长度，然后短按 OK 确认。", () => {
        if (editId) Object.assign(records.find((record) => record.id === editId), values);
        else { const item = Object.assign({}, values, { id: Date.now() }); records.unshift(item); selectedId = item.id; }
        renderRecords(); renderDetail();
      });
    }
  });
  $("#cancelApproval").addEventListener("click", () => cancelApproval());
  $("#demoApprove").addEventListener("click", approveAction);
  $("#menuButton").addEventListener("click", () => setMenuOpen(!$(".sidebar").classList.contains("is-open")));
  $("#navScrim").addEventListener("click", () => setMenuOpen(false));
  document.addEventListener("keydown", (event) => { if (event.key === "Escape") setMenuOpen(false); });
  window.addEventListener("resize", () => { if (window.innerWidth > 760) setMenuOpen(false); });
  $("#detailPanel").addEventListener("click", (event) => { if (window.innerWidth <= 760 && event.target === $("#detailPanel")) $("#detailPanel").classList.remove("mobile-visible"); });
  $("#lockButton").addEventListener("click", async () => {
    let lockedLogo = "";
    try {
      const source = $(".brand-logo")?.currentSrc || $(".brand-logo")?.src || "./assets/cipherport-logo.png";
      const response = await fetch(source, { cache: "force-cache" });
      const blob = await response.blob();
      lockedLogo = await new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(String(reader.result || ""));
        reader.onerror = reject;
        reader.readAsDataURL(blob);
      });
    } catch (_) {}
    if (deviceMode) { try { await deviceApi.disconnect(); } catch (_) {} }
    document.body.innerHTML = `<main style="min-height:100vh;display:grid;place-items:center;padding:24px;color:#dbe9e5;background:#071b17;text-align:center"><div>${lockedLogo ? `<img src="${lockedLogo}" alt="CipherPort" style="display:block;width:88px;height:88px;margin:auto;object-fit:contain" />` : ""}<p style="margin:28px 0 8px;font:700 11px ui-monospace,monospace;letter-spacing:.14em;color:#00e889">SESSION CLOSED</p><h1 style="margin:0;font-size:32px">${tr("设备已锁定", "Device locked")}</h1><p style="color:#829a93">${tr("Wi-Fi 与网页会话已关闭。请在设备上重新开启管理模式。", "Wi-Fi and the web session are closed. Reopen management mode on the device.")}</p></div></main>`;
  });
  $("#helpButton").addEventListener("click", () => toast(tr("连接与安全说明：仅通过设备临时 Wi-Fi 访问；新增、编辑和删除必须在设备上按 OK；锁定或超时会立即断开会话。", "Use the device's temporary Wi-Fi. Add, edit, and delete require OK on the device. Lock or timeout ends the session.")));
  $("#extendSession").addEventListener("click", () => {
    sessionSeconds = Math.min(600, sessionSeconds + 300);
    sessionEnded = false;
    toast(sessionSeconds === 600 ? tr("会话已延长，最长保留 10 分钟", "Session extended to a maximum of 10 minutes") : tr("会话已延长 5 分钟", "Session extended by 5 minutes"));
  });
  $$("input[name='wifiPasswordMode']").forEach((input) => input.addEventListener("change", updateWifiPasswordMode));
  $("#revealRange").addEventListener("input", (event) => $("#revealOutput").textContent = `${event.target.value} ${tr("秒", "sec")}`);
  $$(".switch").forEach((button) => button.addEventListener("click", () => { const on = button.classList.toggle("is-on"); button.setAttribute("aria-checked", String(on)); }));
  $("#saveDisplay").addEventListener("click", async () => {
    if (deviceMode) {
      try {
        await deviceApi.updateDisplaySettings({
          autoLockSeconds: Number($("#autoLockSelect").value),
          revealSeconds: Number($("#revealRange").value),
          keySoundEnabled: $("#keySoundSwitch").classList.contains("is-on"),
        });
        toast(tr(`显示设置已保存；设备闲置 ${$("#autoLockSelect").selectedOptions[0].textContent} 后安全锁定并关屏`, `Display settings saved; device locks and turns off the screen after ${$("#autoLockSelect").selectedOptions[0].textContent} idle`));
      } catch (_) {
        toast(tr("显示设置保存失败", "Failed to save display settings"));
      }
      return;
    }
    stageAction("更新显示设置", `自动锁定时间为 ${$("#autoLockSelect").selectedOptions[0].textContent}；短按设备 OK 后写入。`, () => toast("显示设置已更新"));
  });
  $("#webLanguageSelect").addEventListener("change", (event) => setWebLanguage(event.target.value, false));
  $("#saveLanguage").addEventListener("click", async () => {
    const selectedLanguage = $("#webLanguageSelect").value === "en" ? "en" : "zh-CN";
    const savedLanguage = localStorage.getItem("cipherport-language") === "en" ? "en" : "zh-CN";
    if (deviceMode) {
      try {
        await deviceApi.updateLanguage(selectedLanguage);
        const readback = await deviceApi.getLanguage();
        if (readback.language !== selectedLanguage) throw new Error("language verification failed");
        setWebLanguage(selectedLanguage);
        toast(tr("语言已保存并同步到设备", "Language saved and synchronized to the device"));
      } catch (_) {
        setWebLanguage(savedLanguage);
        toast(tr("语言保存失败", "Failed to save language"));
      }
    } else {
      setWebLanguage(selectedLanguage);
      toast(tr("语言已保存", "Language saved"));
    }
  });
  ["#homeTitle", "#homeSubtitle"].forEach((selector) => $(selector).addEventListener("input", () => {
    $("#homePreviewTitle").textContent = $("#homeTitle").value || "PASSWORD MANAGER";
    $("#homePreviewSubtitle").textContent = $("#homeSubtitle").value || "YOUR KEYS. YOUR CONTROL.";
  }));
  $("#saveHome").addEventListener("click", async () => {
    if (deviceMode) {
      try {
        const expected = `${$("#homeTitle").value}\n${$("#homeSubtitle").value}`.trim();
        await deviceApi.updateWelcome(expected);
        const saved = await deviceApi.getWelcome();
        if (String(saved.message || "") !== expected) throw new Error("welcome verification failed");
        toast(tr("设备首页已更新", "Device home content updated"));
      } catch (_) { toast(tr("首页内容保存失败", "Failed to save home content")); }
    } else {
      stageAction("更新设备首页", "设备将更新欢迎页标题和说明。短按设备 OK 后写入。", () => toast("设备首页已更新"));
    }
  });
  $("#changePin").addEventListener("click", async () => {
    if (deviceMode) {
      try { await deviceApi.requestPinChange(); toast(tr("请在设备上完成 PIN 修改", "Complete the PIN change on the device")); }
      catch (_) { toast(tr("无法启动 PIN 修改", "Could not start PIN change")); }
    } else {
      stageAction("修改 PIN", "请在设备上依次验证当前四位 PIN、输入新 PIN 并再次确认。PIN 不会传到浏览器。", () => toast("设备已进入修改 PIN 流程"));
    }
  });
  $("#clearData").addEventListener("click", async () => {
    if (deviceMode) {
      try { await deviceApi.requestClearData(); toast(tr("请在设备上长按确认清除", "Long-press on the device to confirm erasure")); }
      catch (_) { toast(tr("无法启动数据清除", "Could not start data erasure")); }
    } else {
      stageAction("清除全部数据", "这将永久清除密码库和设备设置。请在设备上完成最终确认。", () => {
        records.splice(0, records.length);
        selectedId = 0;
        renderRecords();
        renderDetail();
        toast("演示数据已清除");
      });
    }
  });
  $$("[data-copy]").forEach((button) => button.addEventListener("click", () => copyText(button.dataset.copy, tr("管理地址已复制", "Management address copied"))));
  setInterval(() => {
    sessionSeconds = Math.max(0, sessionSeconds - 1);
    $("#sessionClock").textContent = `${String(Math.floor(sessionSeconds / 60)).padStart(2, "0")}:${String(sessionSeconds % 60).padStart(2, "0")}`;
    if (sessionSeconds === 0 && !sessionEnded) {
      sessionEnded = true;
      const lockButton = $("#lockButton");
      if (lockButton) lockButton.click();
    }
  }, 1000);
  updateWifiPasswordMode();
  setWebLanguage(webLanguage);
  registerWebMcpTools();
  window.__cipherportReady = true;
  if (deviceMode) void connectDevice();
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initializeApp);
} else {
  initializeApp();
}
