# Cross-machine alerts

Peer test machines publish **actionable alerts** here when a regression or environment-specific bug needs attention on another lab PC.

| File | Purpose |
|------|---------|
| `alerts.jsonl` | Append-only alert log (one JSON object per line) |
| `index.json` | Recent summaries + unread counts per machine |

## Publish (from any test machine)

```powershell
.\tools\debug-harness\post-machine-alert.ps1 `
  -Title "core boot hang on 2号机" `
  -Body "Symptom + suspected commit + fix status." `
  -ToMachine "1号机" `
  -Severity blocker `
  -RelatedCommits "192427a" `
  -RelatedFiles "runtime/module_manager/presentation.cpp" `
  -Push
```

## Read

```powershell
git pull
.\tools\debug-harness\show-machine-alerts.ps1
.\tools\debug-harness\show-machine-alerts.ps1 -Json -MarkRead
```

MCP: `debug_get_machine_alerts`, `debug_post_machine_alert`.

Machine id from `deploy.config.json` → `testMachine`. See [`test-environments.json`](../../test-environments.json).
