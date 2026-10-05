# Player bots — research notes

Research into whether/how to add AI-controlled player bots (party fillers, world
population, etc.) to the `forever` branch, prompted by
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) as an example of
a mature, good-feeling implementation. Not a commitment to port that specific
project — the implementation can take a different route if that fits this fork
better.

- [feasibility-report.md](feasibility-report.md) — verdict, why a direct port is a
  bad fit, and the alternatives worth considering.
- [fork-context.md](fork-context.md) — the facts about this fork's current state
  (core lineage, build system, how far combat/stats have been rewritten) that any
  bot-implementation approach needs to account for.
- [implementation-plan.md](implementation-plan.md) — phased native-implementation
  plan, and what Phase 0 (the hello-world scaffold) actually adds to the tree.
- [environment-setup.md](environment-setup.md) — what to provide (toolchain,
  databases, config, optional client/game-data) to build and test it.
- [login-flow-notes.md](login-flow-notes.md) — Phase 1 de-risking: exactly what
  this fork's real player-login code path does, so the next implementation step
  is grounded rather than guessed.

**Status:** Phase 0 scaffold implemented 2026-10-05 (`BotMgr` singleton + `.bot
hello` GM command gated by a dedicated RBAC permission and a `Bot.Enabled`
config toggle, ticked from `World::Update`). No bot characters exist yet —
see implementation-plan.md for what's real vs. planned, login-flow-notes.md
for the Phase 1 groundwork already done.
