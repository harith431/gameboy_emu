// Auto-select thinking level for the Game Boy emulator project.
//
// Hard tasks (cycle-accurate timing, hardware debugging, test failures) get
// `high` thinking; routine work (edits, docs, running tests) gets `medium`.
//
// Reads the user's prompt each turn and switches the thinking level before the
// agent loop starts. Disable by removing this file or editing HARD_KEYWORDS.

import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";

// Word-boundary match so "stat" doesn't trigger on "status" or "state".
const HARD_PATTERN =
  /\b(ppu|apu|timer|timing|cycle|interrupt|stat|dma|glitch|sweep|envelope|mbc|banking|serial|halt|oam|vram|dmg|lcdon|mem_timing|oam_bug|halt_bug|dmg_sound|acid2|blargg|mooneye|frame sequencer|length counter|edge case|debug|fail|bug|crash|wrong|broken|stuck)\b/i;

export default function (pi: ExtensionAPI) {
  pi.on("before_agent_start", async (event, ctx) => {
    const prompt = event.prompt || "";
    const hard = HARD_PATTERN.test(prompt);
    const level = hard ? "high" : "medium";

    const current = pi.getThinkingLevel();
    if (current !== level) {
      pi.setThinkingLevel(level);
      if (ctx.hasUI) ctx.ui.notify(`Auto thinking → ${level}`, "info");
    }
  });
}
