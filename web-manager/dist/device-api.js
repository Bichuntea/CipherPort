(() => {
  "use strict";

  let sessionToken = "";

  class DeviceApiError extends Error {
    constructor(message, status = 0, payload = null) {
      super(message);
      this.name = "DeviceApiError";
      this.status = status;
      this.payload = payload;
    }
  }

  async function request(path, options = {}) {
    const headers = new Headers(options.headers || {});
    if (sessionToken) headers.set("X-Session-Token", sessionToken);
    const fetchOptions = Object.assign({}, options, {
      headers: headers,
      cache: "no-store",
      credentials: "same-origin",
    });
    const response = await fetch(path, fetchOptions);

    const contentType = response.headers.get("content-type") || "";
    const payload = contentType.includes("application/json")
      ? await response.json()
      : await response.text();

    if (!response.ok) {
      throw new DeviceApiError(`Device request failed (${response.status})`, response.status, payload);
    }
    return payload;
  }

  async function getSession() {
    const session = await request("/api/session");
    if (session && session.status === "authorized" && typeof session.token === "string") {
      sessionToken = session.token;
    }
    return session;
  }

  function normalizeAccount(account, fallbackIndex) {
    return {
      id: Number.isInteger(account.index) ? account.index : fallbackIndex,
      index: Number.isInteger(account.index) ? account.index : fallbackIndex,
      name: String(account.platform || ""),
      note: String(account.note || account.url || ""),
      username: String(account.username || ""),
      password: String(account.password || ""),
    };
  }

  window.CipherPortDeviceApi = Object.freeze({
    DeviceApiError,
    isDeviceHost: () => location.hostname === "192.168.4.1",
    connect: () => request("/api/session/connect", { method: "POST" }),
    getSession,
    disconnect: async () => {
      try {
        return await request("/api/session/disconnect", { method: "POST" });
      } finally {
        sessionToken = "";
      }
    },
    clearSession: () => { sessionToken = ""; },
    listRecords: async () => {
      const payload = await request("/api/accounts");
      return (payload.accounts || []).map(normalizeAccount);
    },
    stageMutation: (mutation) => request("/api/mutations", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(mutation),
    }),
    getMutationStatus: () => request("/api/mutations/status"),
    getWelcome: () => request("/api/settings/welcome"),
    getDisplaySettings: () => request("/api/settings/display"),
    getLanguage: () => request("/api/settings/language"),
    updateWelcome: (message) => request("/api/settings/welcome", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ message }),
    }),
    updateDisplaySettings: (settings) => request("/api/settings/display", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(settings),
    }),
    updateLanguage: (language) => request("/api/settings/language", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ language }),
    }),
    requestPinChange: () => request("/api/actions/change-pin", { method: "POST" }),
    requestClearData: () => request("/api/actions/clear-data", { method: "POST" }),
  });
})();
