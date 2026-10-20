# CI webhooks for Codex

This standalone integration receives real GitHub webhooks. It does not poll GitHub.
It currently monitors pushes to `main` in `wsekta/orm-cxx` and waits for GCC, MSVC,
Quality, Backend conformance, Clang, Packages and Docs. GitHub Pages is a separate
deployment; the resumed agent checks its status before declaring the site deployed.

## Current state

The receiver and bridge are implemented and locally tested. **Production is not
enabled.** Cloudflare currently returns HTTP 403 because its connection lacks
`workers-scripts.write`. The current Windows Desktop session uses a private stdio
app-server and has no shared control socket. Connecting to this existing Desktop
conversation must be verified before installing the bridge. Starting a separate
server against an active Desktop thread is not a supported workaround here.
The actual `codex queue --remote unix://` probe on this installation failed with
`os error 10050`; no message was delivered to the Desktop conversation.

No GitHub webhook or Windows startup task has been enabled yet. The existing
temporary CI heartbeat remains active until the replacement works end to end.

## Architecture

GitHub `push`, `workflow_job` and `workflow_run` events enter `/webhooks/github`.
HMAC-SHA256 verification precedes JSON parsing. Payloads are limited to 2 MiB.
Only configured repository/branch combinations are accepted. A repository webhook
is sufficient for this installation; a GitHub App is optional when installing
centrally across multiple repositories later.

One SQLite Durable Object per repository stores delivery IDs, workflow attempts,
commit state and a durable outbox. A failed matrix job generates an early failure
event. Success requires every configured workflow to finish with `success`;
`skipped` and `neutral` do not count as success. Unknown workflows do not satisfy
the required set. Out-of-order progress and older attempts cannot replace a newer
result. Only push workflows are monitored, not pull-request runs.

The Windows bridge maintains an authenticated outbound WebSocket. There is no
inbound Windows port and no public Codex server. One bridge connection owns each
repository stream. Notifications are sent one at a time and removed from the
outbox only after an acknowledgment. They survive receiver eviction and an offline
computer. Delivery IDs and completed commit states are retained for at least
30 days; unacknowledged outbox records are not expired.
If GitHub cannot reach the public receiver, its failed delivery must be redelivered
from GitHub. Durable replay covers notifications already accepted by the receiver;
it does not replace GitHub's delivery history or promise to recover an event that
never reached it.

The bridge records each notification in local SQLite before sending
`codex queue --remote unix:// --thread UUID --message TEXT`. An exact
repository/SHA route takes precedence over the configured default conversation.
Pushes from other computers or tools go to that default conversation. The
integration never changes code or cancels CI automatically.

Accepted deliveries are not sent again after a lost acknowledgment or restart.
Codex queue does not expose an idempotency key: a crash or timeout during dispatch
leaves an uncertain record. The bridge stops rather than risk sending the same
message twice. Inspect the destination conversation, then set that record to
`accepted` if it arrived, or `received` if it did not. Delivery is not claimed to
be exactly once across that external command boundary.

## Local verification

Requires Node.js 24 or later:

```powershell
cd tools/ci-webhook
npm ci
npm run types
npm run check
npm test
npm run test:bridge
npm run test:transport
npm run bundle
```

The Workers tests run in workerd and cover signed requests, all-workflow
aggregation, matrix failures, reruns, durable outbox replay after eviction,
WebSocket hibernation and acknowledgments. Bridge tests cover local persistence,
offline Codex, ambiguous delivery, SHA routing and external data validation.
The transport test connects the real Node.js bridge to a local Worker over a
real WebSocket, including offline replay and acknowledged delivery to a test
destination. It does not claim to verify delivery to the Desktop app.
The bundle command is a dry run and does not deploy resources.

## Production activation

1. Reconnect Cloudflare with Workers Scripts Write on the intended account.
   Deploy this Worker and its SQLite Durable Object migration. Do not change
   unrelated account resources. `wrangler deploy` can be used with an authorized
   local Wrangler session; a connected Cloudflare API can also upload the bundle.
2. Generate two independent random secrets. Set `GITHUB_WEBHOOK_SECRET` and
   `BRIDGE_TOKEN` as Worker secrets. Neither belongs in Git or command arguments.
   For local runtime tests, copy `.dev.vars.example` to ignored `.dev.vars`.
3. Create a private `bridge.local.json` from `bridge/config.example.json`. Set the
   deployed endpoint, actual Codex executable, default conversation UUID, token
   file and journal path. Keep credentials outside the checkout, with Windows
   access restricted to the account running the bridge. Do not copy Codex account
   credentials into Cloudflare. The receiver only stores CI metadata.
4. Verify the existing Desktop conversation is attached to the shared app-server
   used by `appServer`. Send a harmless test event and verify that it appears in
   that same conversation, including when the conversation is idle. This remains
   a required deployment gate for the current installation.
5. With the Worker healthy and both secrets installed, configure the repository
   webhook idempotently:

   ```powershell
   node scripts/configure-github.mjs --config PATH/bridge.local.json --secret-file PATH/webhook-secret.txt
   ```

   This requires repository webhook administration. It modifies only a webhook
   with this exact receiver URL and subscribes to all three events. It does not
   add notification steps to workflow YAML files. Other repositories can be
   installed explicitly by adding their expected workflows and bridge config.
6. Test a real failed workflow, a successful full matrix, redelivery, two pushes,
   rerun success after failure and a bridge disconnect/reconnect. Disable the
   temporary heartbeat only after live delivery has been confirmed.
7. Install the bridge at Windows logon:

   ```powershell
   ./scripts/install-bridge.ps1 -ConfigPath PATH/bridge.local.json
   ```

   The installer refuses activation without a local control socket, token file
   and healthy receiver. It uses the current Windows user and prevents parallel
   instances. The computer and Codex backend must be available to analyze events;
   Cloudflare continues receiving while they are offline.

For a push originating in another Codex conversation, register its destination
before the push; other pushes continue using the default conversation:

```powershell
node bridge/register.mjs --config PATH/bridge.local.json --sha FULL_SHA --thread THREAD_UUID
```

To stop the integration, stop and disable the scheduled task and deactivate
only the matching repository webhook. Pending outbox data is retained. Do not
delete the Durable Object namespace as a routine shutdown step.

References: [Codex queue](https://learn.chatgpt.com/docs/developer-commands#codex-queue),
[GitHub signatures](https://docs.github.com/en/webhooks/using-webhooks/validating-webhook-deliveries),
[Durable Object WebSockets](https://developers.cloudflare.com/durable-objects/best-practices/websockets/).
