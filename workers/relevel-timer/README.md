# ReLevel release timer

One SQLite-backed Cloudflare Durable Object schedules its next check with a persistent alarm.
It runs without visitors, a local computer, or a cron service. All requests use the fixed
`relevel-release-check` object identity. Concurrent wakes, redeployments and duplicate alarm
delivery reuse its persisted next-check time instead of creating more loops.

The alarm calls the site's `/api/relevel/tick` endpoint every 15 minutes. That endpoint checks
the public verified index and requests the existing GitHub workflow only when a check is due.
Play credentials and private module access remain in CI. Browser status requests merely wake
this same timer when `RELEVEL_TIMER_URL` is configured; they do not create competing checks.
Workflow history, request coalescing, workflow concurrency and per-input caches provide
additional protection against duplicate preparation.

A recovery alarm is persisted before network I/O. Failed requests back off for 1, 2, 4, 8,
then at most 15 minutes;
successful ticks schedule the next check 15 minutes from their start. There is no catch-up
burst after an outage. `/health` reads the stored state without waking or postponing the timer.

Use the **Workers Free** plan and the SQLite storage migration supplied here. Normal operation
is 96 ticks/day, with one small overwritten record and no accumulating history. Requests have
a 25-second deadline and a persisted hard cap of 200 outbound checks/day. Even 96 full-length requests use about 300 GB-s/day versus the Free
plan's 13,000 GB-s/day allowance. Free-plan quota exhaustion fails requests rather than billing
overages. Do not enable a paid Workers plan for this timer.

Deploy and start once:

```sh
npx --yes wrangler@4.157.0 deploy --config workers/relevel-timer/wrangler.jsonc
curl --fail --request POST https://<deployed-worker>/wake
curl --fail https://<deployed-worker>/health
```

Set the site's `RELEVEL_TIMER_URL` to the deployed HTTPS origin, or update its reviewed default
in `api/relevel/_config.js`. Keep the Worker name, binding, migration and object identity stable
when updating. Deploying another copy under a different name would create a second scheduler.

Run `node --test test/relevel-timer.test.js test/relevel-refresh.test.js` for persistent scheduling,
restart, concurrency, failure recovery and dispatch/freshness checks. Wrangler's dry run validates
the actual Worker bundle and binding configuration before deployment.
