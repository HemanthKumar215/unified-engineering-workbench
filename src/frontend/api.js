// Thin transport wrapper around the local C++ backend. All engineering logic lives in C++.
"use strict";

const Api = (() => {
  async function request(method, path, params, body, timeoutMs = 120000) {
    const qs = params ? "?" + new URLSearchParams(params).toString() : "";
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), timeoutMs);
    let res;
    try {
      res = await fetch(path + qs, { method, body, signal: ctrl.signal,
        headers: body ? { "Content-Type": "text/plain; charset=utf-8" } : undefined });
    } catch (e) {
      throw new Error(e.name === "AbortError"
        ? `Request timed out after ${timeoutMs / 1000} s.`
        : "Backend not reachable. Is Launch.exe still running? Restart it and reload this page.");
    } finally {
      clearTimeout(timer);
    }
    const type = res.headers.get("Content-Type") || "";
    if (type.startsWith("application/json")) {
      const data = await res.json();
      if (!res.ok || data.ok === false) {
        const err = new Error(data.error || `HTTP ${res.status}`);
        err.data = data;
        throw err;
      }
      return data;
    }
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.text();
  }

  return {
    get: (path, params, timeoutMs) => request("GET", path, params, undefined, timeoutMs),
    post: (path, params, body, timeoutMs) => request("POST", path, params, body, timeoutMs),
    // Triggers a browser download of a backend-generated file.
    download(path, params) {
      const a = document.createElement("a");
      a.href = path + (params ? "?" + new URLSearchParams(params).toString() : "");
      a.download = "";
      document.body.appendChild(a);
      a.click();
      a.remove();
    },
  };
})();
