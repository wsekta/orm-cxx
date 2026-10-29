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

**Parallel delivery is a candidate upgrade until its deployment is recorded.**
The code below adds durable local intake and independent conversation queues.
The earlier single-conversation proof does not establish this upgrade or delivery
to a second conversation. Verify each intended idle conversation separately in
the current Desktop session before calling its notifications active.

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
remote outbox only after a committed local inbox record is acknowledged. That
ACK means durable receipt by the Windows bridge, not acceptance by Desktop.
Receiver-side pending records survive eviction and an offline computer. After
handoff, the Windows journal holds any still-undelivered result; its disk and
recoverable backups are part of the delivery guarantee. Delivery IDs and completed commit states are retained for at least
30 days; unacknowledged outbox records are not expired.
If GitHub cannot reach the public receiver, its failed delivery must be redelivered
from GitHub. Durable replay covers notifications already accepted by the receiver;
it does not replace GitHub's delivery history or promise to recover an event that
never reached it.

The bridge records each complete notification in SQLite with synchronous
durability before ACK. It then sends `codex queue --remote ENDPOINT --thread UUID
--message TEXT` from independent queues. At most two conversations dispatch at
once; each conversation retains FIFO order once destinations are known,
including when several branches belong to it. An unregistered event has no
destination yet and can be assigned later, after other known deliveries. An installation-wide dispatcher lock is acquired before connecting
to the receiver, so a second process cannot replace the socket or drain the same
queues. Registration commands can still write to the journal concurrently.

Every new notice needs an exact repository/branch/SHA registration. Missing
routes wait locally as `waiting_for_route`; there is no global default-recipient
fallback, including for unregistered main merges. Registration wakes the waiting
event through the bridge's local reconciliation timer. The integration never
changes code or cancels CI automatically. This timer reads only the local inbox;
it does not query GitHub or keep an agent turn waiting.

Accepted deliveries are not sent again after a lost acknowledgment or restart.
Codex queue does not expose an idempotency key: a crash or timeout during dispatch
leaves an uncertain record and pauses that conversation's queue. Other
conversations continue receiving results. Inspect the recorded destination, then
use explicit reconciliation with evidence: `accepted` if the message arrived,
or `received` only after confirming it did not. Safely stop the bridge before
reconciliation; the recovery command requires its exclusive dispatcher lock and
refuses while the bridge is active. `dispatching` records become
uncertain after a restart; they are never automatically resent. Delivery is not
claimed to be exactly once across that external command boundary.

Existing main-only commit state and routes are migrated once into the branch
scope. Pending outbox entries and accepted delivery IDs retain their identities.
Legacy notifications without a branch are interpreted as `main`; their old
SHA-only routes are never used for a feature branch.

## Parallel features and registration

Use one separate Git worktree, feature branch and originating conversation for
each feature. The branch normally uses `codex/<feature>`; an explicitly requested
branch name takes precedence. Do not switch another conversation's checkout.
One shared bridge serves all these worktrees. Creating one bridge per branch
would replace the existing repository connection.

The branch has one active conversation owner. A repeated identical registration
is idempotent. Another conversation cannot silently overwrite an owner or an
exact commit route. Explicit transfer changes future registrations only; old
commit routes and already pinned delivery destinations stay unchanged. A branch
whose old exact routes have inconsistent owners is reported as conflicted until
an explicit compare-and-set transfer names the complete old owner set.
The migrated journal rejects writes from obsolete registration scripts, including
the former `INSERT OR REPLACE` writer. Update worktree pointers to the installed
manifest instead of continuing to invoke an old snapshot directly.

Register each push from its absolute worktree root:

```powershell
node bridge/register.mjs --config PATH/bridge.local.json --worktree ABSOLUTE_WORKTREE --branch FEATURE_BRANCH --sha FULL_SHA --thread ORIGINATING_CONVERSATION_UUID
```

The CLI verifies the configured repository against `origin`, the symbolic branch
and the full current `HEAD` before writing. Detached HEAD, wrong branch, wrong
repository and stale SHA are rejected. It prints the stored registration, not a
delivery claim. Recheck these values before pushing the explicit registered SHA
to its branch ref. After pushing, record that SHA and end the turn immediately;
the webhook resumes its originating conversation. Do not poll CI or run local
verification suites as a substitute.

Operations are scoped explicitly and never send a message by themselves:

```powershell
node bridge/manage.mjs status --config PATH/bridge.local.json --branch FEATURE_BRANCH
node bridge/manage.mjs list --config PATH/bridge.local.json --thread CONVERSATION_UUID
node bridge/manage.mjs close --config PATH/bridge.local.json --branch FEATURE_BRANCH --thread CURRENT_OWNER_UUID
node bridge/manage.mjs transfer --config PATH/bridge.local.json --branch FEATURE_BRANCH --from EXPECTED_OLD_OWNER_UUID --thread NEW_OWNER_UUID
node bridge/manage.mjs adopt --config PATH/bridge.local.json --event WAITING_EVENT_ID --thread AUTHORIZED_CONVERSATION_UUID
node bridge/manage.mjs reconcile --config PATH/bridge.local.json --event UNCERTAIN_EVENT_ID --status accepted --evidence EVIDENCE_TEXT
```

For a conflicted legacy owner set, `transfer --from UUID_A,UUID_B` names all
expected old owners without relying on native Windows JSON argument quoting.

`adopt` registers one selected waiting event after verifying its exact repository,
branch and SHA against a GitHub push run. It works after the branch has advanced
and does not move a checkout. This is the recovery path for manual main merges
or pushes registered after receipt. A failed evidence check leaves routes
unchanged. Reconciliation also accepts `--status received` after confirming no
message arrived. For a legacy uncertain event with an unknown destination,
`--thread EVIDENCED_HISTORICAL_UUID` is mandatory; never use today's route as
proof of its old destination.

Status includes local waiting/ready/dispatching/uncertain/accepted counts and
registered owners/routes. The receiver's `/pending` endpoint alone no longer
describes every undelivered result because an event may already be in the local
inbox. Unresolved events do not expire. Registered, receiver-connected,
locally-received, Desktop-accepted and verified idle wake are distinct facts.

### Installed runtime and new worktrees

Install versioned runtime snapshots outside all feature checkouts. A private
installation manifest named `active-runtime.json` selects the verified snapshot;
do not confuse it with a snapshot's integrity manifest:

```json
{
  "schemaVersion": 2,
  "runtimePath": "C:/private/CodexCiWebhook/runtime/FULL_SHA",
  "nodePath": "C:/private/node.exe",
  "configPath": "C:/private/CodexCiWebhook/bridge.local.json"
}
```

Use the installed entrypoint with that manifest, regardless of a feature's checked
out source version:

```powershell
& PATH/installed-runtime/scripts/webhook.ps1 -ManifestPath PATH/active-runtime.json -Command register -Arguments @('--worktree', 'ABSOLUTE_WORKTREE', '--branch', 'FEATURE_BRANCH', '--sha', 'FULL_SHA', '--thread', 'ORIGINATING_CONVERSATION_UUID')
& PATH/installed-runtime/scripts/webhook.ps1 -ManifestPath PATH/active-runtime.json -Command status -Arguments @('--branch', 'FEATURE_BRANCH')
```

Ignored local `AGENTS.md` and `.scratch/ci-webhook-runtime.md` do not follow a new
worktree automatically. Bootstrap them from an existing instruction-bearing
worktree, preserving any existing differing files:

```powershell
./scripts/bootstrap-worktree.ps1 -Worktree ABSOLUTE_NEW_WORKTREE -SourceWorktree ABSOLUTE_EXISTING_WORKTREE -ManifestPath PATH/active-runtime.json
```

Bootstrap verifies both paths share the same Git common directory and the target
is a registered worktree. It copies only local instructions and writes a runtime
pointer; credentials, configuration and journals stay in their private installed
location. Registration never restarts Desktop or launches another app-server.

### Upgrade and recovery

Prepare the immutable candidate from the exact GitHub-verified source and locked
dependencies without a local build. Stop the old bridge safely before migrating;
keep the webhook, secrets, shared app-server and old source snapshot. Back up the
journal consistently through SQLite, including WAL state. Do not copy only a
live main database file. Read back owners, routes and pending states after the
versioned migration and before the first new-style ACK.

Old ambiguous rows have no reliable historical destination. The dispatcher
refuses cutover until they are reconciled from evidence. Current routes may have
been overwritten by the old implementation and must not be guessed as targets.
The source migration preserves accepted IDs and old exact routes.

The migrated database may be incompatible with the previous runtime even before
the first ACK. A pre-cutover backup can be restored only if no new receipt,
dispatch or custody change happened afterward. After handoff, keep the new inbox
and compatible drain/recovery tooling: simply reverting the binary or restoring
an old database can lose locally held events or duplicate already sent messages.
A return to the old runtime requires compatible schema/state, drained or
reconciled pending events, and no ambiguous in-flight command.

With the normal bridge stopped, drain currently eligible local work without
connecting to the Worker or accepting more upstream custody:

```powershell
node bridge/drain.mjs --config PATH/bridge.local.json
```

The installed wrapper also accepts `-Command drain`. The drain shares the
dispatcher lock and waits for active sends. It leaves missing routes, uncertain
events and future retries pending, reports their reasons and exits with code 2.
A complete local drain still does not prove compatibility with an old schema.

Operational acceptance requires real GitHub events to wake two explicitly
authorized idle Desktop conversations independently, with correct branch/SHA,
actual Desktop tools and no duplicate message. Component tests and queue-command
acceptance do not replace those proofs. A future normal Desktop restart remains
subject to the existing shared-transport limitation above.

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

For a personal desktop shortcut, use `scripts/start-desktop-webhooks.ps1
-ProfilePath ABSOLUTE_PRIVATE_PROFILE`. Invoke it with Windows PowerShell 5.1,
`-NoProfile -ExecutionPolicy Bypass -File`, keeping the console interactive.
The private JSON profile has `schemaVersion: 1`, `launcherDirectory`, `nodePath`,
`pythonPath`, `bridgeConfig`, `stateRoot`, `thread`, `appServer` and an explicit
`ignoreBusyTurns` array containing only individually reviewed historical turns.
The launcher directory must contain the verified `desktop/` helpers, their
`bridge/app-server.mjs` import and the locked `ws` dependency outside checkouts.

The shortcut wrapper opens an installed ordinary Desktop when needed, logs before
preparation, creates a fresh configuration and arms the existing controlled pilot.
It refuses concurrent starters or overlapping application versions. It waits for
history verification and the queued conversation probe, then leaves actual
same-conversation tools/idle-wake verification and bridge activation to the agent.
An early failure stays visible and is saved in the attempt's `start.log` and
`error.txt`. `-NoPause` is available for unattended failure-path checks in GitHub.

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
   deployed endpoint, actual Codex executable, token
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
   rerun success after failure and a bridge disconnect/reconnect. Record the
   real idle-wake proof separately for every configured conversation.
7. Install the bridge at Windows logon:

   ```powershell
   ./scripts/install-bridge.ps1 -ConfigPath PATH/bridge.local.json
   ```

   The installer refuses activation without a ready local app-server endpoint,
   an existing CLI executable, token file and healthy receiver. It uses the current Windows user and prevents parallel
   instances. The computer and Codex backend must be available to analyze events;
   Cloudflare continues receiving while they are offline.

For every push, register its exact branch and SHA in the originating Codex
conversation before pushing; unregistered results wait for an explicit route:

```powershell
node bridge/register.mjs --config PATH/bridge.local.json --worktree ABSOLUTE_WORKTREE --branch FEATURE_BRANCH --sha FULL_SHA --thread THREAD_UUID
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
