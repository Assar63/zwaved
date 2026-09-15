# Security S2 — on-bench acceptance runbook (#189)

The S2 stack (epic [#27]) is **code-complete**: every phase from the crypto
primitives (#179) through the inclusion bootstrap (#187) and the live
transport + SPAN persistence (#199) is implemented and unit-tested. None of
it has been exercised against a real device.

This runbook is the operator-run procedure that closes [#189]. It is written
to be followed top to bottom in one sitting, and to produce a verdict for
each of #189's five checkboxes.

> **Why a human has to do this.** Every step needs something no test can
> reach: a physical button press to put a device into inclusion mode, a DSK
> printed on a sticker, a power cycle. The unit suite proves the daemon's
> *own* halves of these exchanges are self-consistent — controller encrypts,
> test decrypts, and back. It cannot prove the wire bytes match what a
> Silicon Labs device actually expects. That is exactly the gap this closes.

[#27]: https://github.com/Assar63/zwaved/issues/27
[#189]: https://github.com/Assar63/zwaved/issues/189

---

## 0. What you need

| | |
| --- | --- |
| Controller | Aeotec Z-Stick Gen5 (or another accepted dongle, see `[dongles] accept`) |
| S2 Access Control device | A door lock. Its DSK is on the device or its box. |
| S2 Authenticated device | A sensor (motion / door / multisensor). |
| S2 Unauthenticated device | A bulb or plug. |
| Terminal | Two: one running `zwave-terminal`, one running `busctl --system monitor com.tiunda.ZWaved` |

A device that supports S2 advertises CC `0x9F` in its NIF. If a device you
expect to be S2 ends up recorded as `None`, that is a finding — record it
rather than retrying until it works.

> **These steps change your real Z-Wave network.** Inclusion and exclusion
> add and remove nodes for real. Use bench devices, not anything load-bearing
> in your home.

---

## 1. Preparation

Start from a known state so a failure is attributable.

```bash
# Build and install as usual, then stop the daemon before touching state.
sudo systemctl stop zwaved   # or kill your foreground instance

# Back up state — you want to be able to return to it.
sudo cp -a /var/lib/zwaved /var/lib/zwaved.bak.$(date +%s)
```

Turn the log level down to `debug` — the SPAN checkpoint line is logged at
debug, and you want the full handshake visible. In
`/etc/zwaved/zwaved.conf`:

```ini
[logger]
min_level = debug
```

There is no SIGHUP reload, so this must be set before you start the daemon.

Run it in the foreground. The bootstrap logs under `[s2-bootstrap]`, the SPAN
store under `[span-store]`.

```bash
sudo ZWAVED_STATE_DIR=/var/lib/zwaved ./cmake-build-gnu/zwaved 2>&1 | tee ~/s2-acceptance.log
```

In a second terminal:

```bash
busctl --system monitor com.tiunda.ZWaved | tee ~/s2-acceptance.dbus.log
```

In a third, `zwave-terminal` — you need it for the DSK prompt in §2.

Confirm the keys came up. On a first run the daemon generates them:

```
[s2] generated one or more network keys in /var/lib/zwaved/security/s2 — back
them up; losing them forces re-inclusion (with DSK) of every secure node
```

(On a later run it logs `[s2] loaded the network keys from ...` instead.)

**Back that directory up now.** Losing the S2 keys forces re-inclusion of
every secure node — the SPAN state is disposable, the keys are not.

---

## 2. Checkbox 1 — Include an S2 Access Control lock with DSK confirmation

This is the hardest path and the one most likely to surface a wire bug: it
exercises KEX negotiation, ECDH, the DSK obfuscation/restore ritual, the
encrypted temp channel, and a per-class key install.

1. Issue the inclusion:
   ```bash
   busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
       com.tiunda.ZWaved1 AddNode yyyayay 1 0 42 0 0
   ```
2. Put the lock into inclusion mode (usually a button sequence — see its
   manual).
3. Watch for `NodeInclusionStatus` progressing, then the S2 handshake in the
   daemon log: `KEX_GET` → `KEX_REPORT` → `KEX_SET` → `PUBLIC_KEY_REPORT`.
4. **`zwave-terminal` raises a DSK banner** and the daemon emits a retained
   `DSKPendingConfirmation` carrying the partial DSK. The first 5-digit group
   is blanked — that is the half you supply.
5. Press `[k]` in `zwave-terminal`, read the first 5-digit group off the
   lock's DSK label, type it.
6. The handshake resumes: KEX echo verify → `NETWORK_KEY_REPORT` →
   `NETWORK_KEY_VERIFY` → `TRANSFER_END`.

**Pass when** the node ends up recorded as Access Control:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetNodeInfo y <nodeId>
```

`securityScheme` should be `4` (S2 Access Control). A `NodeSecurityStatus`
signal with `scheme=4` should also be in the monitor log.

**Also try the failure path** — it is cheap here and is the MITM protection
the whole ritual exists for. Re-run with a deliberately wrong (but
well-formed) 5-digit PIN. Expect the handshake to abort at the encrypted
KEX echo with `KEX_SET echo mismatch — KEX_FAIL`, **not** to succeed and
**not** to hang. Exclude and redo with the correct PIN before moving on.

| Scheme value | Class |
| --- | --- |
| 0 | None |
| 1 | S0 |
| 2 | S2 Unauthenticated |
| 3 | S2 Authenticated |
| 4 | S2 Access Control |

---

## 3. Checkbox 2 — Include an S2 Authenticated sensor

Same flow, also DSK-gated. Expect `securityScheme = 3`.

If the sensor is battery-powered it sleeps between wake-ups. The interview
(#203) is enqueued to `PendingQueue` and drains on the next
`WAKE_UP_NOTIFICATION`, so `GetNodeInfo` may show an incomplete capability
set until then. That is expected and is not an S2 failure — note it and move
on.

---

## 4. Checkbox 3 — Include an S2 Unauthenticated bulb

No DSK prompt on this path: the grant is un-obfuscated, so key agreement runs
straight through. Expect `securityScheme = 2`, and a noticeably shorter
handshake in the log.

Confirm it is actually controllable over the encrypted channel:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetSwitchBinary yb <nodeId> true
```

The bulb should physically turn on. In the log the payload goes out through
`SecurityS2OutboundOrchestrator` as a `0x9F` frame — if you see the raw
`0x25` Binary Switch payload on the wire instead, encryption is being
bypassed and that is a **fail**.

---

## 5. Checkbox 4 — All three survive a daemon restart

This is what SPAN persistence (#199) bought.

1. With all three nodes included and responsive, stop the daemon **cleanly**
   (`SIGTERM` / `systemctl stop`, not `SIGKILL`).
2. In the shutdown log, expect: `[span-store] saved N SPAN(s) at shutdown`.
3. Start it again. Expect:
   `[span-store] restored N SPAN(s) — S2 peers resume without a Nonce-Sync resync`.
4. Without any other interaction, control each of the three nodes.

**Pass when** each responds on the *first* command, with no Nonce-Sync
round-trip in the log beforehand. A resync here would mean persistence isn't
taking effect.

Optionally verify the rows directly:

```bash
sudo sqlite3 /var/lib/zwaved/nodes.db \
    "SELECT peer_node_id, length(state) FROM span_state;"
```

Each row should be 32 bytes.

> Note the checkpoint interval is **60 s**, so a node included and used
> within the last minute before an *unclean* kill may legitimately have no
> persisted SPAN. That is the documented tradeoff, not a bug — use a clean
> shutdown for this check.

---

## 6. Checkbox 5 — A forced nonce desync recovers via Nonce Sync

Prove the SOS recovery path works, because it is what protects you when
persistence *doesn't* save you (unclean shutdown, dropped frames).

1. Stop the daemon cleanly.
2. Wipe the SPAN table only — keys and node registry stay:
   ```bash
   sudo sqlite3 /var/lib/zwaved/nodes.db "DELETE FROM span_state;"
   ```
3. Start the daemon. Expect **no** "restored N SPAN(s)" line.
4. Send a command to one of the secure nodes.

**Pass when** the log shows a `NONCE_GET` / `NONCE_REPORT` exchange followed
by the command succeeding — one extra round-trip, then normal service. The
device must end up in the commanded state.

**Fail** if the command is silently dropped, the daemon retries forever, or
the node stops responding until re-inclusion.

---

## 7. Recording the result

For each checkbox record **pass / fail / not-tested**, the device model, and
for any failure the relevant log window from `~/s2-acceptance.log`. Post that
to [#189].

A failure here is a wire-correctness bug in a specific phase, not a reason to
redo the epic — the log will name the phase (`[s2-bootstrap]`, the inbound or
outbound orchestrator, `[span-store]`). File it against that phase's issue
and link it from #189.

If all five pass, #189 closes and with it epic #27, leaving only the optional
MPAN work (#188).

---

## Appendix — cleaning up

To return the bench to its starting state:

```bash
# Exclude each test node
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveNode yyy 1 0 43
# ...then press the device's exclusion button.

# Or restore wholesale
sudo systemctl stop zwaved
sudo rm -rf /var/lib/zwaved
sudo mv /var/lib/zwaved.bak.<timestamp> /var/lib/zwaved
```
