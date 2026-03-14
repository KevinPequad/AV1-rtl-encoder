Resume the same mission in the same repository.

Rules for this turn:
- Read `.codex-supervisor/status.json` first if it exists, then continue from the highest-priority unfinished encoder work.
- Do not spend this turn only summarizing or re-planning if implementation, verification, commit, or push work is still possible.
- Keep `README.md` and any other required project status docs aligned with the true repo state.
- Before ending this turn, rewrite `.codex-supervisor/status.json` with the latest `status`, `summary`, `next_prompt`, `blocker`, `last_verified`, `needs_push`, and optional `sleep_seconds`.
- If the last `next_prompt` is stale, ignore it and choose the current highest-leverage next step yourself.

Continue immediately.
