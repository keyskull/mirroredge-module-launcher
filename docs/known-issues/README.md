# å·²çŸ¥é—®é¢˜ç™»è®°å†Œï¼ˆKnown Issues Registryï¼‰

ç»“æž„åŒ–è®°å½•**å·²éªŒè¯æ ¹å› ã€æœ‰æ•ˆä¿®å¤ã€ä»¥åŠå·²å¤±è´¥æ–¹æ¡ˆ**ï¼Œä¾› AI ä¸Žå¼€å‘è€…åœ¨åŠ¨æ‰‹æ”¹ä»£ç å‰å…ˆæ£€ç´¢ï¼Œé¿å…é‡å¤ä¿®å¤æˆ–é‡è¯•å·²çŸ¥æ— æ•ˆè·¯å¾„ã€‚

**æµç¨‹è¯´æ˜Žï¼š** [known-issues-workflow.md](../known-issues-workflow.md)

## ç´¢å¼•

| ID | çŠ¶æ€ | æ ‡é¢˜ | åŒºåŸŸ |
|----|------|------|------|
| [KI-2026-001](KI-2026-001-d3d9-hook-inject-worker.md) | resolved | D3D9 hook / è®¾å¤‡æŽ¢æµ‹ä¸å¾—åœ¨ inject worker çº¿ç¨‹ | mmultiplayer / d3d9 |
| [KI-2026-002](KI-2026-002-alt-tab-device-lost.md) | resolved | Module Manager èœå•æ‰“å¼€æ—¶ Alt+Tab å¡æ­» | module_manager |
| [KI-2026-003](KI-2026-003-chinese-ime-stuck.md) | resolved | æ³¨å…¥åŽä¸­æ–‡ IME å…¨å±€å¤±æ•ˆ | module_manager |
| [KI-2026-004](KI-2026-004-find-device-proxy-mode.md) | resolved | ä»£ç†æ¨¡å¼ä¸‹ FindDeviceSafe å¯¼è‡´æ¸¸æˆé€€å‡º | module_manager |
| [KI-2026-005](KI-2026-005-mp-playthrough-ingameplay.md) | open | mp-playthrough-bots æ— æ³•åˆ¤å®šè¿›å…³ / inGameplay | harness / multiplayer |
| [KI-2026-006](KI-2026-006-core-boot-probe-globals.md) | resolved | Core loaded ä½† `init: game not ready yet` æ— é™å¾ªçŽ¯ | sdk / core / borderless |
| [KI-2026-007](KI-2026-007-ipc-thread-ensure-hooks.md) | resolved | ENSURE_MP_HOOKS åœ¨ IPC çº¿ç¨‹æ‰§è¡Œå¯¼è‡´æ ¸å¿ƒåˆå§‹åŒ–å¤±è´¥ | core / mod_ipc |
| [KI-2026-008](KI-2026-008-startup-splash-harness-gap.md) | investigating | å¯åŠ¨ splash å¡ä½ä½† harness æœªæ£€æµ‹ | harness / module_manager |
| [KI-2026-009](KI-2026-009-engine-tab-unsafe-engine-scan.md) | resolved | Engine æ ‡ç­¾é¡µè£¸ `GetEngine()` æŸ¥æ‰¾å¯¼è‡´å´©æºƒ | core / module_manager / sdk |
| [KI-2026-010](KI-2026-010-module-manager-console-keyboard-input.md) | open | Module Manager console æ‰“ä¸å¼€å­— / é”®ç›˜è¾“å…¥ä¸åˆ° ImGui | module_manager / input |
| [KI-2026-011](KI-2026-011-rdp-block-creatdevice.md) | resolved | RDP session blocks CreateDevice â€” "Message" dialog | d3d9 / injection / harness |
| [KI-2026-012](KI-2026-012-soft-freeze-after-bot-despawn.md) | resolved | Bot disconnect Despawn soft-freeze after TransformBones | multiplayer / engine |
| [KI-2026-013](KI-2026-013-tdpawn-remote-spike.md) | removed | TdPawn remote spike deleted 2026-07-22 — mesh-only remotes | multiplayer / engine |

æ–°å¢žæ¡ç›®ï¼šå¤åˆ¶ [`_template.md`](_template.md)ï¼Œåœ¨æœ¬è¡¨å¢žåŠ ä¸€è¡Œï¼Œå¹¶åœ¨ [troubleshooting.md](../troubleshooting.md) æ·»åŠ äº¤å‰é“¾æŽ¥ï¼ˆè‹¥é¢å‘ç”¨æˆ·ç—‡çŠ¶ï¼‰ã€‚

## ä¸Ž troubleshooting.md çš„åˆ†å·¥

| æ–‡æ¡£ | ç”¨é€” |
|------|------|
| **known-issues/** | å®Œæ•´è°ƒæŸ¥å²ã€**å¤±è´¥æ–¹æ¡ˆè¡¨**ã€éªŒè¯æ—¥æœŸã€é˜²é‡å¤ |
| **troubleshooting.md** | ç—‡çŠ¶ â†’ å¿«é€Ÿä¿®å¤æŸ¥è¡¨ï¼›å¯é“¾æŽ¥åˆ° KI æ¡ç›® |

ç®€å•ä¸€æ¬¡æ€§é—®é¢˜ï¼ˆè·¯å¾„é”™è¯¯ã€ç¼º DirectX SDKï¼‰åªå†™ troubleshooting å³å¯ã€‚æ›¾å¯¼è‡´å¤šæ¬¡é”™è¯¯å°è¯•æˆ– AI æ˜“å›žå½’çš„é—®é¢˜ï¼Œå¿…é¡»å»º KI æ¡ç›®ã€‚



