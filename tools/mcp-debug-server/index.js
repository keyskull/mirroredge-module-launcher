#!/usr/bin/env node
/**
 * MCP server: runtime debug tools for mirroredge-module-launcher.
 */

import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import net from "node:net";
import { randomBytes } from "node:crypto";

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT =
  process.env.MMOD_REPO_ROOT ||
  path.resolve(__dirname, "..", "..");

const TEST_LOGS_INDEX_PATH = path.join(REPO_ROOT, "test-logs", "index.json");
const TEST_LOGS_ALERTS_DIR = path.join(REPO_ROOT, "test-logs", "alerts");
const TEST_LOGS_ALERTS_JSONL = path.join(TEST_LOGS_ALERTS_DIR, "alerts.jsonl");
const TEST_LOGS_ALERTS_INDEX = path.join(TEST_LOGS_ALERTS_DIR, "index.json");
const TEST_ENVIRONMENTS_PATH = path.join(REPO_ROOT, "test-environments.json");

const DEBUG_DIR = path.join(
  process.env.TEMP || process.env.TMP || "C:\\Temp",
  "mirroredge-debug"
);
const DUMP_DIR = path.join(
  process.env.TEMP || process.env.TMP || "C:\\Temp",
  "mirroredge-dumps"
);
const HANG_REPORT_DIR = path.join(DEBUG_DIR, "hang-reports");
const LAST_SESSION_PATH = path.join(DEBUG_DIR, "last-session.json");
const SCENARIOS_DIR = path.join(REPO_ROOT, "tools", "debug-harness", "scenarios");

const PIPE_NAMES = {
  manager: "\\\\.\\pipe\\mirroredge_module_manager_control",
  mmultiplayer: "\\\\.\\pipe\\mirroredge_module_control",
};

function readLastSession() {
  if (!fs.existsSync(LAST_SESSION_PATH)) {
    return null;
  }
  let text = fs.readFileSync(LAST_SESSION_PATH, "utf8");
  if (text.charCodeAt(0) === 0xfeff) {
    text = text.slice(1);
  }
  return JSON.parse(text);
}

function writeSessionManifest(sessionId, logPath) {
  fs.mkdirSync(DEBUG_DIR, { recursive: true });
  const manifest = {
    sessionId,
    logPath,
    startedAt: Date.now(),
  };
  fs.writeFileSync(LAST_SESSION_PATH, JSON.stringify(manifest));
  return manifest;
}

function listScenarios() {
  if (!fs.existsSync(SCENARIOS_DIR)) {
    return [];
  }
  return fs
    .readdirSync(SCENARIOS_DIR)
    .filter((name) => name.endsWith(".ps1"))
    .map((name) => name.replace(/\.ps1$/, ""))
    .sort();
}

function tailLog(logPath, lines = 50) {
  if (!fs.existsSync(logPath)) {
    return [];
  }
  const content = fs.readFileSync(logPath, "utf8");
  const all = content.split(/\r?\n/).filter(Boolean);
  return all.slice(-lines);
}

function resolveLogPath({ sessionId, logPath }) {
  if (logPath) {
    return logPath;
  }
  const last = readLastSession();
  if (sessionId) {
    return path.join(DEBUG_DIR, `${sessionId}.ndjson`);
  }
  if (last?.logPath) {
    return last.logPath;
  }
  return null;
}

function resolveInteractionLogPath(sessionId) {
  const last = readLastSession();
  const id = sessionId || last?.sessionId;
  if (id) {
    return path.join(DEBUG_DIR, `${id}-interactions.ndjson`);
  }
  return path.join(DEBUG_DIR, "interactions.ndjson");
}

function parseNdjsonLines(rawLines) {
  const entries = [];
  for (const line of rawLines) {
    try {
      entries.push(JSON.parse(line));
    } catch {
      entries.push({ _raw: line, _parseError: true });
    }
  }
  return entries;
}

function queryLog(logPath, options = {}) {
  const {
    lines = 50,
    scanLines = 2000,
    component,
    hypothesisId,
    locationContains,
    messageContains,
  } = options;

  if (!logPath || !fs.existsSync(logPath)) {
    return { logPath, matches: [], scanned: 0 };
  }

  const content = fs.readFileSync(logPath, "utf8");
  const all = content.split(/\r?\n/).filter(Boolean);
  const window = all.slice(-scanLines);
  const entries = parseNdjsonLines(window);

  const filtered = entries.filter((entry) => {
    if (entry._parseError) {
      return false;
    }
    if (component && entry.component !== component) {
      return false;
    }
    if (hypothesisId && entry.hypothesisId !== hypothesisId) {
      return false;
    }
    if (
      locationContains &&
      !(entry.location || "").includes(locationContains)
    ) {
      return false;
    }
    if (
      messageContains &&
      !(entry.message || "").includes(messageContains)
    ) {
      return false;
    }
    return true;
  });

  return {
    logPath,
    scanned: window.length,
    matchCount: filtered.length,
    matches: filtered.slice(-lines),
  };
}

function parseModLogResponse(response) {
  if (!response) {
    return [];
  }
  return response
    .split(/\r?\n/)
    .filter((line) => line && line !== "END")
    .map((line) => line.trim())
    .filter(Boolean);
}

function listCrashDumps() {
  if (!fs.existsSync(DUMP_DIR)) {
    return [];
  }
  return fs
    .readdirSync(DUMP_DIR)
    .filter((name) => name.toLowerCase().endsWith(".dmp"))
    .map((name) => {
      const full = path.join(DUMP_DIR, name);
      const stat = fs.statSync(full);
      return {
        name,
        path: full,
        sizeBytes: stat.size,
        modifiedAt: stat.mtimeMs,
      };
    })
    .sort((a, b) => b.modifiedAt - a.modifiedAt);
}

function listHangReports() {
  if (!fs.existsSync(HANG_REPORT_DIR)) {
    return [];
  }
  return fs
    .readdirSync(HANG_REPORT_DIR, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => {
      const full = path.join(HANG_REPORT_DIR, entry.name);
      const stat = fs.statSync(full);
      const summaryPath = path.join(full, "hang-summary.json");
      let summary = null;
      if (fs.existsSync(summaryPath)) {
        try {
          summary = JSON.parse(fs.readFileSync(summaryPath, "utf8"));
        } catch {
          summary = null;
        }
      }
      return {
        name: entry.name,
        path: full,
        modifiedAt: stat.mtimeMs,
        summary,
      };
    })
    .sort((a, b) => b.modifiedAt - a.modifiedAt);
}

function readHangReport(bundlePath) {
  const resolved = path.resolve(bundlePath);
  const files = [
    "hang-summary.json",
    "interaction-hang-tail.jsonl",
    "debug-log-tail.txt",
    "window-hang-context.json",
    "pipe-hang-context.json",
    "manager-log.txt",
    "manager-log.error.txt",
  ];
  const out = { bundlePath: resolved, files: {} };
  for (const name of files) {
    const full = path.join(resolved, name);
    if (!fs.existsSync(full)) {
      continue;
    }
    const text = fs.readFileSync(full, "utf8");
    if (name.endsWith(".json")) {
      try {
        out.files[name] = JSON.parse(text);
      } catch {
        out.files[name] = text;
      }
    } else if (name.endsWith(".jsonl")) {
      out.files[name] = text
        .split(/\r?\n/)
        .filter(Boolean)
        .map((line) => {
          try {
            return JSON.parse(line);
          } catch {
            return { _raw: line };
          }
        });
    } else {
      out.files[name] = text;
    }
  }
  return out;
}

function invokePipe(pipeName, command, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const client = net.connect(pipeName);
    let response = "";
    let settled = false;

    const timer = setTimeout(() => {
      if (!settled) {
        settled = true;
        client.destroy();
        reject(new Error(`pipe timeout (${timeoutMs}ms): ${pipeName}`));
      }
    }, timeoutMs);

    client.on("connect", () => {
      client.write(`${command}\n`);
    });

    client.on("data", (chunk) => {
      response += chunk.toString("utf8");
      if (response.includes("\n")) {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
          client.end();
          resolve(response.split(/\r?\n/)[0]);
        }
      }
    });

    client.on("error", (err) => {
      if (!settled) {
        settled = true;
        clearTimeout(timer);
        reject(err);
      }
    });

    client.on("end", () => {
      if (!settled) {
        settled = true;
        clearTimeout(timer);
        resolve(response.trim());
      }
    });
  });
}

function runScenario(scenario, extraArgs = [], envOverrides = {}) {
  const script = path.join(REPO_ROOT, "tools", "debug-harness", "run.ps1");
  return new Promise((resolve, reject) => {
    const args = [
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      script,
      scenario,
      ...extraArgs,
    ];
    const child = spawn("powershell.exe", args, {
      cwd: REPO_ROOT,
      env: { ...process.env, ...envOverrides },
    });

    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => {
      stdout += d.toString();
    });
    child.stderr.on("data", (d) => {
      stderr += d.toString();
    });
    child.on("close", (code) => {
      resolve({
        exitCode: code ?? 1,
        stdout: stdout.trim(),
        stderr: stderr.trim(),
        pass: code === 0,
      });
    });
    child.on("error", reject);
  });
}

function readRegistryMachineIds() {
  if (!fs.existsSync(TEST_ENVIRONMENTS_PATH)) {
    return [];
  }
  try {
    const registry = JSON.parse(fs.readFileSync(TEST_ENVIRONMENTS_PATH, "utf8"));
    return (registry.machines || [])
      .map((entry) => entry?.id)
      .filter(Boolean);
  } catch {
    return [];
  }
}

function readMachineAlerts(options = {}) {
  const { forMachine = "", limit = 20 } = options;
  if (!fs.existsSync(TEST_LOGS_ALERTS_JSONL)) {
    return { alerts: [], indexPath: TEST_LOGS_ALERTS_INDEX };
  }

  const lines = fs
    .readFileSync(TEST_LOGS_ALERTS_JSONL, "utf8")
    .split(/\r?\n/)
    .filter(Boolean);

  let alerts = [];
  for (const line of lines) {
    try {
      alerts.push(JSON.parse(line));
    } catch {
      /* skip malformed */
    }
  }

  if (forMachine) {
    alerts = alerts.filter(
      (a) => !a.toMachine || a.toMachine === "" || a.toMachine === forMachine
    );
  }

  return {
    indexPath: TEST_LOGS_ALERTS_INDEX,
    alerts: alerts.slice(-limit),
  };
}

function readHarnessTestStatus() {
  const registryIds = readRegistryMachineIds();
  if (!fs.existsSync(TEST_LOGS_INDEX_PATH)) {
    return {
      repoRoot: REPO_ROOT,
      indexPath: TEST_LOGS_INDEX_PATH,
      updatedAt: null,
      machines: registryIds.map((id) => ({
        machine: id,
        suite: "(no index)",
        pass: "-",
        passAll: false,
        commit: "-",
        finishedAt: "-",
        dirty: false,
      })),
      error: "test-logs/index.json not found",
    };
  }

  let index;
  try {
    index = JSON.parse(fs.readFileSync(TEST_LOGS_INDEX_PATH, "utf8"));
  } catch (err) {
    return {
      repoRoot: REPO_ROOT,
      indexPath: TEST_LOGS_INDEX_PATH,
      error: err instanceof Error ? err.message : String(err),
    };
  }

  const machinesMap = index.machines || {};
  const ids =
    registryIds.length > 0
      ? registryIds
      : Object.keys(machinesMap);

  const rows = ids.map((id) => {
    const entry = machinesMap[id];
    if (!entry) {
      return {
        machine: id,
        suite: "(no run in index)",
        pass: "-",
        passAll: false,
        commit: "-",
        finishedAt: "-",
        dirty: false,
      };
    }
    return {
      machine: id,
      suite: entry.suite || "",
      pass: `${entry.passCount ?? "?"}/${entry.totalCount ?? "?"}`,
      passAll: !!entry.pass,
      commit: entry.shortCommit || "",
      finishedAt: entry.finishedAt || "",
      dirty: !!entry.dirty,
      runId: entry.runId || "",
    };
  });

  for (const id of Object.keys(machinesMap)) {
    if (ids.includes(id)) {
      continue;
    }
    const entry = machinesMap[id];
    rows.push({
      machine: `${id} (unregistered)`,
      suite: entry.suite || "",
      pass: `${entry.passCount ?? "?"}/${entry.totalCount ?? "?"}`,
      passAll: !!entry.pass,
      commit: entry.shortCommit || "",
      finishedAt: entry.finishedAt || "",
      dirty: !!entry.dirty,
      runId: entry.runId || "",
    });
  }

  return {
    repoRoot: REPO_ROOT,
    indexPath: TEST_LOGS_INDEX_PATH,
    updatedAt: index.updatedAt || null,
    schemaVersion: index.schemaVersion ?? null,
    machines: rows,
  };
}

const server = new McpServer({
  name: "mirroredge-debug",
  version: "1.4.0",
});

server.tool(
  "debug_get_harness_test_status",
  "Read cross-machine harness pass/fail summary from test-logs/index.json (see test-logs/README.md).",
  {},
  async () => {
    const status = readHarnessTestStatus();
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(status, null, 2),
        },
      ],
      isError: !!status.error && !status.machines?.length,
    };
  }
);

server.tool(
  "debug_get_last_session",
  "Read last mirroredge debug session manifest (sessionId, logPath, startedAt).",
  {},
  async () => {
    const session = readLastSession();
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            session
              ? { repoRoot: REPO_ROOT, scenarios: listScenarios(), ...session }
              : {
                  repoRoot: REPO_ROOT,
                  scenarios: listScenarios(),
                  error: "No session manifest found",
                  expectedPath: LAST_SESSION_PATH,
                },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_initialize_session",
  "Create a new debug session id + log path manifest for harness runs.",
  {
    sessionId: z.string().optional(),
  },
  async ({ sessionId }) => {
    const id = sessionId || randomBytes(6).toString("hex");
    fs.mkdirSync(DEBUG_DIR, { recursive: true });
    const logPath = path.join(DEBUG_DIR, `${id}.ndjson`);
    const manifest = writeSessionManifest(id, logPath);
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            {
              repoRoot: REPO_ROOT,
              ...manifest,
              env: {
                MMOD_DEBUG_SESSION: id,
                MMOD_DEBUG_LOG: logPath,
              },
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_list_scenarios",
  "List available debug harness scenario scripts.",
  {},
  async () => {
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            {
              repoRoot: REPO_ROOT,
              scenarios: listScenarios(),
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_tail_log",
  "Tail NDJSON agent debug log lines from the current or specified session.",
  {
    lines: z.number().int().min(1).max(500).optional(),
    sessionId: z.string().optional(),
    logPath: z.string().optional(),
  },
  async ({ lines = 50, sessionId, logPath }) => {
    const resolved = resolveLogPath({ sessionId, logPath });
    const tail = resolved ? tailLog(resolved, lines) : [];
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            {
              logPath: resolved,
              lineCount: tail.length,
              lines: tail,
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_query_log",
  "Filter NDJSON agent debug log by component, hypothesisId, or substring (scans recent lines).",
  {
    lines: z.number().int().min(1).max(500).optional(),
    scanLines: z.number().int().min(50).max(10000).optional(),
    sessionId: z.string().optional(),
    logPath: z.string().optional(),
    component: z.string().optional(),
    hypothesisId: z.string().optional(),
    locationContains: z.string().optional(),
    messageContains: z.string().optional(),
  },
  async ({
    lines = 50,
    scanLines = 2000,
    sessionId,
    logPath,
    component,
    hypothesisId,
    locationContains,
    messageContains,
  }) => {
    const resolved = resolveLogPath({ sessionId, logPath });
    const result = queryLog(resolved, {
      lines,
      scanLines,
      component,
      hypothesisId,
      locationContains,
      messageContains,
    });
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(result, null, 2),
        },
      ],
    };
  }
);

server.tool(
  "debug_get_mod_log",
  "Read module_manager in-memory log ring buffer via GET_LOG control pipe.",
  {
    maxLines: z.number().int().min(1).max(500).optional(),
    timeoutMs: z.number().int().min(1000).max(60000).optional(),
  },
  async ({ maxLines = 120, timeoutMs = 10000 }) => {
    try {
      const response = await invokePipe(
        PIPE_NAMES.manager,
        `GET_LOG ${maxLines}`,
        timeoutMs
      );
      const logLines = parseModLogResponse(response);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                target: "manager",
                lineCount: logLines.length,
                lines: logLines,
              },
              null,
              2
            ),
          },
        ],
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                target: "manager",
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "debug_tail_interactions",
  "Tail harness interaction log (*-interactions.ndjson) from the current or specified session.",
  {
    lines: z.number().int().min(1).max(500).optional(),
    sessionId: z.string().optional(),
    phase: z.string().optional(),
    action: z.string().optional(),
  },
  async ({ lines = 50, sessionId, phase, action }) => {
    const resolved = resolveInteractionLogPath(sessionId);
    const tail = tailLog(resolved, lines * 4);
    let entries = parseNdjsonLines(tail);
    if (phase) {
      entries = entries.filter((e) => e.phase === phase);
    }
    if (action) {
      entries = entries.filter((e) => e.action === action);
    }
    entries = entries.slice(-lines);
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            {
              logPath: resolved,
              lineCount: entries.length,
              entries,
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_list_dumps",
  "List crash minidumps captured by run-with-dump.ps1 under %TEMP%\\mirroredge-dumps.",
  {},
  async () => {
    const dumps = listCrashDumps();
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify({ dumpDir: DUMP_DIR, dumps }, null, 2),
        },
      ],
    };
  }
);

server.tool(
  "debug_list_hang_reports",
  "List hang triage bundles exported by hang-guard under hang-reports/.",
  {},
  async () => {
    const reports = listHangReports();
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            { hangReportDir: HANG_REPORT_DIR, reports },
            null,
            2
          ),
        },
      ],
    };
  }
);

server.tool(
  "debug_read_hang_report",
  "Read hang triage bundle: summary + steps 1-4 files (interaction, NDJSON, window, pipe/log).",
  {
    bundlePath: z.string(),
  },
  async ({ bundlePath }) => {
    try {
      const report = readHangReport(bundlePath);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(report, null, 2),
          },
        ],
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                bundlePath,
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "mod_control",
  "Send a command to module_manager or mmultiplayer control pipe (PING, GET_STATUS, INJECT, etc.).",
  {
    command: z.string(),
    target: z.enum(["manager", "mmultiplayer"]).optional(),
    timeoutMs: z.number().int().min(1000).max(60000).optional(),
  },
  async ({ command, target = "manager", timeoutMs = 10000 }) => {
    const pipe = PIPE_NAMES[target];
    try {
      const response = await invokePipe(pipe, command, timeoutMs);
      let parsed = response;
      if (command === "GET_STATUS") {
        try {
          parsed = JSON.parse(response);
        } catch {
          parsed = response;
        }
      } else if (command === "GET_LOG" || command.startsWith("GET_LOG ")) {
        parsed = parseModLogResponse(response);
      }
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ target, command, response: parsed }, null, 2),
          },
        ],
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                target,
                command,
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "debug_run_scenario",
  "Run a debug harness scenario script (verify-harness, smoke-split, inject-mp, ...).",
  {
    scenario: z.string(),
    skipLaunch: z.boolean().optional(),
    skipBuild: z.boolean().optional(),
    sessionId: z.string().optional(),
  },
  async ({ scenario, skipLaunch, skipBuild, sessionId }) => {
    const extra = [];
    if (skipLaunch) extra.push("-SkipLaunch");
    if (skipBuild) extra.push("-SkipBuild");

    const envOverrides = {};
    if (sessionId) {
      const logPath = path.join(DEBUG_DIR, `${sessionId}.ndjson`);
      envOverrides.MMOD_DEBUG_SESSION = sessionId;
      envOverrides.MMOD_DEBUG_LOG = logPath;
      writeSessionManifest(sessionId, logPath);
    }

    try {
      const result = await runScenario(scenario, extra, envOverrides);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ scenario, ...result }, null, 2),
          },
        ],
        isError: !result.pass,
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                scenario,
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "debug_run_auto_loop",
  "Run harness auto-loop: retry scenarios, optional rebuild, write failure bundles for triage.",
  {
    scenarios: z.array(z.string()).optional(),
    maxRetries: z.number().int().min(0).max(5).optional(),
    rebuildOnFail: z.boolean().optional(),
    continueOnFail: z.boolean().optional(),
    skipLaunch: z.boolean().optional(),
    skipBuild: z.boolean().optional(),
  },
  async ({
    scenarios,
    maxRetries = 2,
    rebuildOnFail,
    continueOnFail,
    skipLaunch,
    skipBuild,
  }) => {
    const extra = ["-MaxRetries", String(maxRetries)];
    if (rebuildOnFail) extra.push("-RebuildOnFail");
    if (continueOnFail) extra.push("-ContinueOnFail");
    if (skipLaunch) extra.push("-SkipLaunch");
    if (skipBuild) extra.push("-SkipBuild");
    if (scenarios?.length) {
      extra.push("-Scenarios", scenarios.join(","));
    }

    try {
      const result = await runScenario("auto-loop", extra);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ scenario: "auto-loop", ...result }, null, 2),
          },
        ],
        isError: !result.pass,
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                scenario: "auto-loop",
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "debug_get_machine_alerts",
  "Read cross-machine alerts from test-logs/alerts/alerts.jsonl (peer lab coordination).",
  {
    forMachine: z.string().optional(),
    limit: z.number().int().min(1).max(100).optional(),
  },
  async ({ forMachine, limit }) => {
    const result = readMachineAlerts({
      forMachine: forMachine || "",
      limit: limit || 20,
    });
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify({ repoRoot: REPO_ROOT, ...result }, null, 2),
        },
      ],
    };
  }
);

server.tool(
  "debug_get_memory_faults",
  "Query in-game SEH memory fault list via module_manager GET_STATUS (memoryFaults).",
  {
    timeoutMs: z.number().int().min(500).max(30000).optional(),
  },
  async ({ timeoutMs }) => {
    try {
      const response = await invokePipe(
        PIPE_NAMES.manager,
        "GET_STATUS",
        timeoutMs || 5000
      );
      const parsed = JSON.parse(response);
      const faults = parsed.memoryFaults || [];
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                repoRoot: REPO_ROOT,
                memoryFaultCount: parsed.memoryFaultCount ?? faults.length,
                memoryFaults: faults,
              },
              null,
              2
            ),
          },
        ],
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
);

server.tool(
  "debug_post_machine_alert",
  "Append a cross-machine alert via post-machine-alert.ps1 (git-tracked in test-logs/alerts/).",
  {
    title: z.string(),
    body: z.string(),
    severity: z.enum(["info", "warning", "blocker", "resolved"]).optional(),
    toMachine: z.string().optional(),
    relatedCommits: z.array(z.string()).optional(),
    relatedFiles: z.array(z.string()).optional(),
    push: z.boolean().optional(),
  },
  async ({
    title,
    body,
    severity,
    toMachine,
    relatedCommits,
    relatedFiles,
    push,
  }) => {
    const script = path.join(
      REPO_ROOT,
      "tools",
      "debug-harness",
      "post-machine-alert.ps1"
    );
    const psArgs = [
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      script,
      "-Title",
      title,
      "-Body",
      body,
      "-Severity",
      severity || "warning",
    ];
    if (toMachine) {
      psArgs.push("-ToMachine", toMachine);
    }
    if (relatedCommits?.length) {
      psArgs.push("-RelatedCommits");
      psArgs.push(...relatedCommits);
    }
    if (relatedFiles?.length) {
      psArgs.push("-RelatedFiles");
      psArgs.push(...relatedFiles);
    }
    if (push) {
      psArgs.push("-Push");
    }

    const result = await new Promise((resolve, reject) => {
      const child = spawn("powershell.exe", psArgs, { cwd: REPO_ROOT });
      let stdout = "";
      let stderr = "";
      child.stdout.on("data", (d) => {
        stdout += d.toString();
      });
      child.stderr.on("data", (d) => {
        stderr += d.toString();
      });
      child.on("close", (code) => {
        resolve({
          exitCode: code ?? 1,
          stdout: stdout.trim(),
          stderr: stderr.trim(),
          pass: code === 0,
        });
      });
      child.on("error", reject);
    });

    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            { repoRoot: REPO_ROOT, title, ...result },
            null,
            2
          ),
        },
      ],
      isError: !result.pass,
    };
  }
);

const transport = new StdioServerTransport();
await server.connect(transport);
