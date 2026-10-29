# CI webhooks for Codex

This standalone integration receives real GitHub webhooks. It does not poll GitHub.
It monitors pushes to every branch except `gh-pages` in `wsekta/orm-cxx`. GCC,
MSVC, Quality, Backend conformance, Clang and Packages are required on each branch;
`main` additionally requires Docs. Quality validates documentation on feature
branches. GitHub Pages publication remains a separate, main-only deployment.

Work on one feature branch, push implementation and fixes to that same branch,
then create a pull request after its current commit passes the required push
workflows. Pull-request checks still run on GitHub, but do not participate in the
earlier push result. Creating a PR is not required to start branch CI or receive
its webhook. Merging the PR remains a separate decision.

## Current state

**The controlled Desktop pilot passed on 2026-10-28.** The same conversation
resumed automatically after restart, its persisted history and attachments passed
comparison, and real Desktop tools worked in the resumed turn. The bridge has an
authenticated connection to the deployed Cloudflare receiver; the repository
webhook is enabled for `push`, `workflow_job` and `workflow_run`.

**End-to-end GitHub CI delivery was verified on `main` on 2026-10-28.** A real
seven-workflow success for `64b78da5f7b838bea51c77ff2fc556b0dd73fef9` automatically
resumed the same Desktop conversation and was acknowledged by the bridge. Branch
coverage also requires the matching receiver policy and bridge version; a health
endpoint, ping or bridge connection alone does not establish that delivery.

The pilot owns this particular Desktop session. Bridge startup at Windows logon
does not make a later ordinary Desktop launch use the shared endpoint; prepare
and launch the pilot again for that new session before relying on notifications.

The bridge accepts an existing local Unix control socket or a literal loopback
WebSocket (`ws://127.0.0.1:PORT` or `ws://[::1]:PORT`). It never starts a server.
The optional Windows Desktop pilot below can sequentially replace Desktop's
private stdio server with one shared server. It never opens the active
conversation in a second server while the original server is running.

## Architecture

GitHub `push`, `workflow_job` and `workflow_run` events enter `/webhooks/github`.
HMAC-SHA256 verification precedes JSON parsing. Payloads are limited to 2 MiB.
Only configured repository/branch combinations are accepted. A repository webhook
is sufficient for this installation; a GitHub App is optional when installing
centrally across multiple repositories later.

One SQLite Durable Object per repository stores delivery IDs, workflow attempts,
commit state scoped by branch and SHA, and a durable outbox. A failed matrix job
generates an early failure event. Success requires every configured workflow to finish with `success`;
`skipped` and `neutral` do not count as success. Unknown workflows do not satisfy
the required set. Out-of-order progress and older attempts cannot replace a newer
result. The same SHA on two branches has independent state and notification IDs.
Only push workflows are monitored. A failed job is attributed to a push only
after its exact run and attempt are confirmed by a push `workflow_run`; an
out-of-order job is retained until that confirmation. PR jobs cannot overwrite
the push result for the same branch and SHA.

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
`codex queue --remote ENDPOINT --thread UUID --message TEXT`. An exact
repository/branch/SHA route takes precedence over the configured default
conversation.
Pushes from other computers or tools go to that default conversation. The
integration never changes code or cancels CI automatically.

Accepted deliveries are not sent again after a lost acknowledgment or restart.
Codex queue does not expose an idempotency key: a crash or timeout during dispatch
leaves an uncertain record. The bridge stops rather than risk sending the same
message twice. Inspect the destination conversation, then set that record to
`accepted` if it arrived, or `received` if it did not. Delivery is not claimed to
be exactly once across that external command boundary.

Existing main-only commit state and routes are migrated once into the branch
scope. Pending outbox entries and accepted delivery IDs retain their identities.
Legacy notifications without a branch are interpreted as `main`; their old
SHA-only routes are never used for a feature branch.

## Verification

The repository verifies substantive changes in GitHub Actions (`Quality`). Do not
run local builds, tests, coverage or static-analysis suites unless the user
explicitly requests them. These are the component's CI commands; they are also
available for explicitly requested local troubleshooting:

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
python -m unittest discover -s test -p '*_test.py' -v
```

The Workers tests run in workerd and cover signed requests, all-workflow
aggregation, matrix failures, reruns, durable outbox replay after eviction,
WebSocket hibernation and acknowledgments. Bridge tests cover local persistence,
offline Codex, ambiguous delivery, SHA routing and external data validation.
The transport test connects the real Node.js bridge to a local Worker over a
real WebSocket, including offline replay and acknowledged delivery to a test
destination. It does not claim to verify delivery to the Desktop app.
The bundle command is a dry run and does not deploy resources.
Quality uploads its checked Worker bundle as
`ci-webhook-worker-FULL_SHA-attempt-N`. Deploy that exact GitHub artifact when
updating the receiver; a local build is not part
of the feature workflow. During the first upgrade from a main-only receiver,
record any temporary bootstrap branch coverage and restore the full configured
policy after deploying the verified artifact. Do not claim all-branch delivery
until the receiver and running bridge both use that policy and protocol.
If the old receiver processed feature commits during bootstrap, supply
`LEGACY_COMMIT_BRANCHES` as a JSON map from every such SHA to its actual branch
on the first new-code deployment. The one-time migration uses that hint instead
of incorrectly importing those commits as `main`.

## Optional Windows Desktop pilot

This is an opt-in operational procedure, not a stable Desktop integration.
App-server WebSocket transport is experimental. The pilot also depends on
Desktop's `CODEX_APP_SERVER_WS_URL` startup setting and its tool-channel bootstrap;
updates can require a new preparation. Do not enable it without accepting those
constraints and a controlled Desktop restart.

Requirements: Windows, the installed Desktop package, its matching relocated CLI,
Node.js 24+, Python 3.12+, an existing private bridge configuration, and the exact
destination conversation UUID. Keep pilot configuration, logs and backups outside
the checkout. The preparation script restricts the pilot directory to its owner
and SYSTEM. It captures the existing server arguments and process creation times;
it does not read account secrets or another process's environment.
Preparation resolves physical paths for the configuration, runtime and bridge
storage. MSIX can redirect newly written AppData files into a private package
cache; the independent task must use the physical files visible to both contexts.
Use the exact configuration path printed by preparation for `--config`.
Other tools can have redirected settings too. If GitHub CLI loses its existing
login outside the packaged app, point `GH_CONFIG_DIR` at the physical directory
containing its existing `hosts.yml`; do not copy credentials or request a new login
before checking that location.

```powershell
./desktop/prepare.ps1 -BridgeConfig PATH/bridge.local.json -Thread THREAD_UUID `
  -PythonPath PATH/python.exe -NodePath PATH/node.exe -StateDirectory PATH/private-pilot
node desktop/launch.mjs --config PATH/private-pilot/desktop.local.json --arm
```

`--arm` asks Windows Task Scheduler to start a hidden supervisor in the current
user's interactive session, with ordinary user permissions. The task has no
schedule or logon trigger and does not poll CI. Its wrapper stays alive with the
supervisor and removes its own task registration on exit. This is necessary
because a detached child can still belong to Desktop's Windows Job Object and
be terminated when Desktop closes. Direct `--switch` invocation is refused
without the task identity.

The supervisor waits for all persisted turns to finish (up to three minutes).
Historical incomplete turns can only be excluded with
explicit, exact `-IgnoreBusyTurns` IDs after checking they are inactive; never
exclude the current turn or a whole conversation. No GitHub state is polled.

The supervisor validates the matching four-file CLI runtime by content hash,
checks the loopback port, then asks Windows Restart Manager to close only the
identified Desktop process without force. A refusal or a different affected
process aborts the switch. It waits for the old app-server to exit before copying
the conversation's rollouts, state/history SQLite databases and WAL/SHM files.
Backups are never restored over conversation files automatically.

Desktop starts with a process-local WebSocket setting and a deep link to the
existing UUID. The supervisor discovers its new tool pipe and verifies the
server PID, then starts the matching real CLI with the original plugin settings
and that pipe. It checks the same idle conversation, pages its stored turns and
compares persisted history, attachments and the old JSONL prefix. Missing history
or an unsuccessful startup triggers a return to normal Desktop startup, provided
all replacement processes can be stopped safely. A blocked return is recorded in
`status.json` and never escalates to forcekill or another simultaneous writer.

The final step sends one authorized queue probe to that UUID. The resumed agent
must verify the nonce, actual Desktop tools, permissions and idle wake. Queue
acceptance only sets `phase: awaiting-conversation-verification` and leaves
`notificationsActive: false`. The supervisor does not activate GitHub webhooks,
install autostart, or claim that CI passed. Inspect `status.json` and private logs
before retrying; an existing launcher lock prevents a concurrent switch.
After an interrupted attempt, compare process creation times before archiving a
stale lock and status. A PID alone can have been reused. Prepare the pilot again
to capture the current Desktop and app-server identities before another attempt.

After successful operational verification, enable the prepared GitHub webhook
and install the bridge normally. A plain `GET /readyz` only checks listener
readiness; it does not establish the conversation's identity or delivery.

## Production activation

1. Ensure Cloudflare has Workers Scripts Write on the intended account.
   Deploy the Worker and its SQLite Durable Object migration if needed. Do not change
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

   The installer refuses activation without a ready local app-server endpoint,
   an existing CLI executable, token file and healthy receiver. It uses the current Windows user and prevents parallel
   instances. The computer and Codex backend must be available to analyze events;
   Cloudflare continues receiving while they are offline.

For every push, register its exact branch and SHA in the originating Codex
conversation before pushing; unregistered pushes use the default conversation:

```powershell
node bridge/register.mjs --config PATH/bridge.local.json --branch FEATURE_BRANCH --sha FULL_SHA --thread THREAD_UUID
```

To stop the integration, stop and disable the scheduled task and deactivate
only the matching repository webhook. Pending outbox data is retained. Do not
delete the Durable Object namespace as a routine shutdown step.

References: [Codex queue](https://learn.chatgpt.com/docs/developer-commands#codex-queue),
[Codex app-server](https://learn.chatgpt.com/docs/app-server),
[Windows Restart Manager](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmshutdown),
[Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects),
[MSIX AppData redirection](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes),
[GitHub signatures](https://docs.github.com/en/webhooks/using-webhooks/validating-webhook-deliveries),
[Durable Object WebSockets](https://developers.cloudflare.com/durable-objects/best-practices/websockets/).
