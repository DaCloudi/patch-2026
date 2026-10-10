# Patch 2026 technical guidance

This is the common guide for coding agents and maintainers. `CLAUDE.md` forwards
here. Read README.md, this file and the evidence beside a hook before changing
native behavior. Technical maintenance guidance belongs in this file; README.md
is for installation, configuration and troubleshooting by end users.

## What this is

An injected Windows x64 DLL and standalone injector for Rec Room **20260323.14**.
The patch routes selected client requests to configured compatible services and
restores measured client behaviors after official-service retirement. It does not
modify original client binaries on disk. Preserve original source checkouts and client installations.
Honor workspace deployment restrictions: infrastructure work belongs on development
hosts/resources, never production. No operator backend or RSA key is compiled in.

The current release build has been tested in multiplayer with remote machines.
The maintainer confirms that its redirects and client modifications function
properly in those sessions. Treat this as a tested, playable patch for the
supported client with compatible configured services.

Build validation is not gameplay validation. The multiplayer confirmation comes
from the maintainer's live testing, not compilation or the agent's startup checks.
It does not establish coverage of every feature, mode or server configuration.
Retest affected behavior after changes; do not carry this confirmation forward
automatically to a different build. Earlier investigation notes below describe
the evidence available at that stage, not the current release's playability.

## Build and distribution

Install Visual Studio C++ x64 tools, then use:

```powershell
./format.ps1 -Check
./build.cmd -Package
```

`build.cmd` forwards to `build.ps1`; CI uses the same batch entry point. Resolve
script-relative defaults after parameter binding for Windows PowerShell support.
The script finds C++ tools through
vswhere, initializes x64 tools, compiles MinHook as C and compiles first-party code
with `/W4 /O2 /MT /EHsc /std:c++17`. Explicit compiler inputs define the build.
MinHook is the only vendored dependency; native calls use the measured Unity gates.

Intermediates belong in ignored `x64/Build`; binaries and the archive belong in
`x64/Release`. Root `2026patch.ini`, `RecRoomScreen.bat` and `RecRoomVR.bat` are
distribution templates. Never commit an operator's configured INI as the default.
`-ConfigurationFile <ini>` packages explicit settings; `-TachyonPublicKey <xml>`
inserts only a public key. Packaging rejects private RSA fields before copying
output. Raw captures, credentials, private keys and development artifacts stay out
of Git and release files.

`Patch26Dist.zip` must contain exactly `2026patch.dll`, `Injector.exe`,
`2026patch.ini`, `RecRoomScreen.bat` and `RecRoomVR.bat`. Use the explicit allowlist,
never archive the output directory. Root launchers differ only in
`+forcemode:screen` versus `+forcemode:vr`; keep shared behavior synchronized.
They preflight, start the injector, wait for its readiness event, then start the
game from the installation directory. That event does not establish hook readiness.
Manual launch is also supported: start Injector.exe first, keep it running and
then start RecRoom.exe from the same installation. User-facing prompts must cover
this path as well as the automatic batch launchers.

CI checks formatting and builds/packages the native binaries. Releases run only
for `v*` tags. Research/test sources are excluded from this source snapshot.
Earlier automated checks recorded below are completed validation, not the current
CI suite. Keep local investigation tooling outside the published tree.

## Architecture and boot sequence

| File under `2026Patch/src` | Responsibility |
| --- | --- |
| `main.cpp` | DLL lifecycle, startup worker, configuration and fingerprint checks |
| `RR/Patching/Patches.cpp` | Runtime resolution, call gates and hook implementation |
| `RR/Patching/Patches.h` | Initialization contract; no header-defined hook state |
| `RR/Methods.h` | Verified module RVAs and metadata slots |
| `RR/Offsets.h` | Byte displacements grouped by owning object |
| `RR/Config.h`, `RR/NetworkConfig.h` | INI loading, routing/key validation |
| `RR/BuildGuard.h` | Shared immutable three-module fingerprint list |
| `Utils/Logging.h` | Bounded session diagnostics shared with startup |
| `Utils/DesktopCapture.h` | Windows JPEG capture and resource cleanup |
| `Utils/deps/minhook` | Vendored hook dependency; preserve upstream/license text |

The standalone injector is `Injector/injector.cpp`. Configuration and batch
launchers live at the repository root; this guide owns technical maintenance notes.

DllMain runs under the Windows loader lock. It creates the worker and closes its
handle without waiting. Never move file hashing, INI reads, DNS, waits or hook
installation into DllMain; never join the worker there. The DLL is process-lifetime:
live unloading is unsupported. Detach handling must not join workers or perform
MinHook/GDI+ teardown, particularly during process termination.

The worker resolves the DLL directory, not the process working directory, resets
the log, loads configuration, sets verbosity and checks loaded-module fingerprints.
Missing/truncated DLL paths stop startup. Configuration/fingerprint failures install
no hooks. `Patches::Initialize` then copies settings, resolves dependencies, creates
hooks and applies their queue. A loaded DLL proves only LoadLibrary succeeded;
read `2026patch.log` for initialization status and errors.

## Client identity, module bases and layout

Injector preflight hashes files without loading game DLLs. The patch repeats the
check using each loaded module's on-disk path. All three fingerprints must match:

| Module | SHA-256 |
| --- | --- |
| GameAssembly.dll | `A6D90770CDFDA4043EADB278A16198ED0907C3BA2AC09BC37D06CD4DD22A1D84` |
| Referee.dll | `5868C90A7C67757F3B678AA356CBD933D646C9749E0ACAA31B7DCC9EAEE12EAD` |
| UnityPlayer.dll | `30F4A99C49566B2C5045469B5A39572DD2298ADC31F7FE2BC39D8BB8FC9D75BB` |

These checks authorize the measured build, not unchanged relocated memory pages.
Never disable them to make another version launch. New client support requires
re-deriving native signatures, addresses, fields and runtime invariants.

`Methods::Unity` values use UnityPlayer, `Methods::Referee` values use Referee;
other method groups use GameAssembly. Metadata entries are pointer slots, not
callable methods. `Offsets` entries are field displacements, not module RVAs.
Equal numbers in different owning types are distinct fields: retain the measured
casts/widths. Never apply an address table from another client build. Preserve
obfuscated-symbol mappings and disassembly evidence.

## Injector discovery and early Referee bootstrap

The injector matches unpatched `RecRoom.exe` by its full configured path and skips
processes already containing current/legacy patch DLLs. A path mismatch can leave
it waiting indefinitely; check launch configuration before changing detection.
Attach attempts are serialized by `Local\Patch2026Injector`. The path-derived
readiness event means the injector is watching, not that patch initialization is
complete. Do not launch or terminate unrelated clients during process discovery.

The measured retired-service arena seam is Referee slot `0x6C10858`, dereference
instruction `0x69839D`. The injector checks bytes `41 8B 00` and an initialized
pointer slot before writing. An existing nonzero arena is retained. Otherwise it
allocates a 64 MiB local arena with three channel descriptors at stride `0x1500030`.
Each channel has queues at `+0xA0` and `+0x100040`, lengths `0x80000` and `0x1000000`.
Both capacities are `(length - 0x1C) / 2`: zero lengths underflowed and caused delayed
Referee faults at `0x62BB0B`/`0x216D035`. Prepare all headers before publishing the
arena pointer. The local queues have no service consumer; preserve normal full-queue
handling and do not invent service approval.

LoadLibraryW can be forwarded to another system module. Remote injection resolves
its actual owning module, then applies the function's relative offset to that
module in the target process. An injector-local absolute pointer is not a remote
address contract. Module presence after loading still says nothing about hook readiness.

## Calling IL2CPP and installing hooks

Unity's API table is at `0x1E9D890`. Initialization waits at most five seconds for
the known string-allocator slot, then validates resolved API pointers against the
verified GameAssembly image. The executable gate arena is `0x1981840`, using
32-byte slots with a 60-slot limit. A nonzero slot or exhausted arena fails instead
of overwriting code. Historical schema probes used an overlapping gate range and
must not be loaded alongside the patch.

Gates supply Windows x64 shadow space and forward register arguments only. Hidden
MethodInfo and stack arguments matter: never reuse a function-pointer signature
without checking the native body and caller. Gate allocation is not synchronized;
parallel creation needs a separate synchronization/ABI review. Be sparing with
managed calls, particularly redundant calls for logging, because exceptions may
unwind across native call boundaries.

Capture metadata resolution belongs on the client's attached capture task, not
the unmanaged startup worker. Managed reference replacement must use the verified
IL2CPP write barrier. Strings have length `+0x10` and UTF-16 characters `+0x14`;
bound the count and validate conversion before routing or certificate-scope checks.

Create and queue hooks while resolving dependencies. Apply the queue only after
callback pointers, settings and endpoint preparation are complete. Pre-apply
failure paths uninitialize MinHook; failed apply is reported but is not a guarantee
of transactional rollback. Preserve native signatures and original behavior on
every branch outside the measured override. C++ catch under `/EHsc` does not contain
Windows access violations. Keep callback-specific SEH guards and failure evidence;
do not treat broad C++ catch as pointer validation.

## Network configuration and distinct request paths

The INI beside the DLL must exist; routing fields can independently be missing or
blank. `ServerUrl` redirects the stock HTTP discovery origin `ns.rec.net`, preserving
its suffix. Discovery supplies other service endpoints. `ServerDomain` scopes
domain-based overrides: equality or a dot-separated subdomain is required, excluding
lookalike suffixes. An empty domain grants no scope. Do not turn protocol identifiers
such as stock discovery hosts or API routes into operator defaults.

`CachedServerDomain` migrates only recognized cached service hosts to the configured
domain, retaining prefixes and ports. Already-current hosts take precedence when
the old domain is a parent of the new one. Keep unrelated URLs unchanged. BestHTTP
Uri replacement uses a fresh native Uri and the write barrier; do not assume every
network stack goes through that hook. Investigate a failing request's actual path
instead of assuming one hook intercepts every network stack.

Certificate pin bypass applies only to configured service hosts. The native image
verifier receives bytes, not a URL, so `AllowUnsignedImages` is global; do not claim
domain restrictions. Both flags can retain original checks. Paths, query strings
and headers may contain account data and must not be logged.

`TachyonHost` accepts hostname/IPv4 plus port 1-65535. The measured transport calls
IPAddress.Parse; resolve hostnames to IPv4 before room entry. IPv6 overrides are
unsupported. Blank transport ID keeps matchmaking's server ID. Remote overrides
require RSA public XML with only Modulus/Exponent; loopback may retain the client's
key. Private RSA fields belong on the server and are rejected even when other
transport settings are absent.

Environment overrides `PATCH26_TACHYON_ENDPOINT` and `PATCH26_TACHYON_PUBLIC_KEY`
follow the INI. Any local tooling that sets them must pass the same environment
to injector/client and restore the caller's values on failure. The shipped launchers
use the INI. Do not log the public-key file or contents.

The historical Photon overlay label is misleading: the observed room path uses
Unity Netcode/LiteNetLib with Tachyon and Dissonance integration. ConnectRoom reads
target room `+0x30`, DTO voice address `+0x40` and server ID `+0x48`. Overlay `none`
before room selection is expected. Do not force online state or fake a successful
connection. Photon app IDs alone do not supply approval/key exchange, Netcode
spawn/RPC contracts or voice transport. Keep those backend requirements distinct
from URL rewriting and image/certificate compatibility.

## Restored behavior and deliberately narrow overrides

Friend requests use native `0x953F250` instead of refusal wrapper `0x2065C70`.
Profile action `0x2456CC0` has a separate inlined refusal: replace only None/null
relationship while retaining pending/accept/cancel/unfriend actions. Return the
actual native promise; never manufacture a successful friendship.

Handshake gesture 4 is also inlined in `0x1E30980`, bypassing request `0x1E2E3C0`.
Both patched paths call receive handler `0x1E2DD90`, preserving ownership, live-hand
checks, midpoint, player identity, roles and communication restrictions. Its
four-argument gate avoids unsupported stack forwarding. Other gestures retain
the full original callback signature. Changes to these paths need two-player
regression testing of delivery and persistence.

Roomie HasNoEnergy `0x2778370` normally selects locomotion-blocking activity 12.
Only a recognized no-credit manager fallback retains local activities; it grants
neither energy balances nor conversation access. Shared wheel getter `0x127D460`
overrides only return site `0x1F99718`, checking availability and no-credit mode.
Keep it noinline: `_ReturnAddress()` restricts the override to the wheel caller.
Changes to these paths need live regression checks of summon/recall, following
and interactions.

Circuit declaration removal `0x114FEB0` skips only before registry `+0x78` exists;
initialized removals/errors stay native. Successful configured-host Test Case
Management writes clear case/run caches `0x9569FE0` and expire plans `0x956AA00`.
Immediate redraw of the current category remains unimplemented.

Desktop capture replaces unsupported System.Drawing behavior at `0x1A03670` with
Windows JPEG encoding. Preserve selected-monitor bounds, width/quality, aspect
ratio and request-driven timing. Resolve Byte[] metadata on the capture task.
Restore selected GDI objects and release every bitmap/DC/stream per frame. FPS
zero installs no refresh hook; 1-15 overrides preserve adaptive throttling and
unsent-frame backpressure and do not guarantee delivered FPS.

The measured module-scan callback `0x203C270` only performs fatal exit 533223478 and
is replaced. Other FatalApplicationQuit reasons and ordinary exits remain native.
Native-fault observation records evidence without swallowing faults or changing
Referee control flow. Do not broadly suppress errors to conceal a backend failure.

## Diagnostics, validation and editing conventions

`2026patch.log` is DLL-relative, reset per launch, capped at 1 MiB and serialized
by a shared lock. Default messages cover startup, connection transitions and
failures. `Verbose=1` enables routine HTTP/hook diagnostics. Capture emits a first
frame and sampled failures; module-scan/no-credit notices emit once. Never log
credentials, paths/queries/headers, key contents or image bytes.

Use drop-in `Injector.exe --check-install` for read-only installation/configuration
preflight. Formatting/build/package checks verify source and distribution integrity,
not gameplay. Behavior changes need a restarted fingerprinted client and exercise
of the affected path. Record observed signatures/fields and unverified outcomes in
this guide and hook comments. Raw captures and local tooling stay outside the
published tree. Preserve evidence from rejected seams so future work does not
repeat them, while clearly distinguishing historical observations from current tests.

Use four-space indentation, attached braces and left-aligned pointer/reference
types. `format.ps1` covers first-party patch/injector code and excludes deps.
`.clang-format` disables namespace-ending comments. Avoid redundant namespace
labels and line-by-line narration; retain detailed comments on measured behavior,
ABI/layout assumptions, resource ownership, scope and failure handling. Keep
MinHook's license/upstream formatting, blank operator defaults, native FPS defaults
and the explicit five-file distribution boundary intact.


## Recorded verification and investigation evidence

### Current release validation

On 2026-10-09, the maintainer confirmed that the exact release build was tested
in multiplayer with remote machines and that its redirects and client
modifications function properly. This is live multiplayer validation, beyond
the earlier desktop authentication, dorm-entry and startup checks recorded here.
The report does not provide a feature-by-feature checklist; do not invent one
or interpret older, narrower observations as a current multiplayer limitation.

### Earlier development checks

Before development test sources were removed from this snapshot, checks passed
for routing/domain boundaries (including lookalike and nested domains), endpoint
bounds, missing routing fields, truncated INI reads, public/private RSA filtering
and remote key requirements. Actual supported module files were accepted, while
incompatible installations and missing files were rejected. The Windows capture
check decoded 20 JPEG frames, preserved aspect ratio and found no GDI handle leak.
Separate native child processes loaded the actual DLL and verified configuration
and missing-module rejection before hooks. Packaging used the five-file allowlist
with blank operator endpoints/key and FPS zero. These results do not replace
current in-client testing after behavior changes or imply those suites run in CI.

Runtime recovery found 419 assemblies through the Unity IL2CPP table. MethodInfo's
native body pointer is at `+0x58` on this fingerprint. Capture metadata and method
signatures from the running supported client; preserve exact pointer widths,
hidden arguments and observed caller restrictions rather than trusting similarly
named obfuscated types. Raw captures stay local and may contain private material.

An earlier dorm failure returned matchmaking code 8718 (FailedToSpawnPlayer) after
RequestMasterSpawnNewLocalPlayer hit a null reference. Malformed API responses and
missing endpoints can abort boot before spawning; do not suppress downstream
errors and assume a transport socket fixes the service contract.

### Transport fields and approval observations

The connector is `RecRoom.RoomLoading.PhotonClients.TachyonClient` despite its
historical namespace. MoveNext body `0x9AA42B0` (wrapper `0xB090E0`) takes the
offline branch at `0x9AA4A36` if VoiceConnectionInfo is empty. NetcodeImpl
`HVFCFCFDHNM` uses setter `EEYJFFZNJED(bool)` at `0x9A95950`; observation must
forward it rather than force the state.

| Surface | Measured contract |
| --- | --- |
| Match DTO | VoiceConnectionInfo `+0x40`, VoiceServerId `+0x48`, Experiments `+0x50` |
| LiteNetLibTransport | Port `+0x30`, Address `+0x38`; StartClient `0x8BACEE0`, StartServer `0x8BAD270` |
| NetworkManager | StartClient `0xB042E90`, StartHost `0xB043080`, StartServer `0xB043260` |
| Tachyon AuthPayload | AccountId, AccessToken, VerificationBlob, ClientKeyA, ClientIVA, ClientPublicKey |
| ConnectionResponsePayload | Reason, SessionKeyB |
| Dissonance | DissonanceNFGO runs alongside Netcode; these earlier observations did not separately verify live microphone relay |

Earlier local server work completed LiteNetLib protocol 13 and Netcode approval.
Datagrams ended in plaintext marker `11` or Rabbit IV plus marker `2A`. Approval
used JSON DR/KT; session key derivation XORed client key A with server key B.
Client public-key decryption continued the AES-CBC chain from the encrypted token's
last block. These are recorded backend observations, not instructions to install a
different cipher/key in this patch or fabricate approval without the service.

Named message kinds 3/4 carried proxied room events, 5/6 room data. The measured
server-to-client proxied header was int32 sender, byte event, int32 cache index
(reader `0x3D33C10`), then unchanged tagged payload. Isolating rejected messages
within Netcode batches let later requests proceed and the development client enter
the dorm. At that investigation stage, unsupported routing operations remained;
that dorm-entry check alone did not establish arbitrary-room or multiple-client
correctness. See current release validation above for later multiplayer testing.
Dissonance ushort reader `0x32B1F90`
and writer `0x32B3BA0` use big-endian order, unlike the Netcode room wrappers.

### Title-screen timing and screen-share evidence

Address getter `LCNUPQPPZSS` (`0x9A972F0`) obtains the transport through
`PXCIBBOTYOG` (`0x9A97A10`), tests the Unity component and formats its address/port.
Without that component it returns none at `0x9A973BE`; the offline branch is
separate. Native connected `0x9AA2CE0`, disconnected `0x9AA3390` and failure
`0x9AA3470` callbacks represent actual room transport outcomes. A development run
observed configured DNS/key readiness before Play and connected callback afterward,
with account approval and Dissonance handshake/state packets. That individual
development run did not verify multiple-client room state or live microphone
relay; it is historical evidence, not the scope of later release testing.

The shipped System.Drawing bitmap constructor `0xA9BC170` always threw
PlatformNotSupportedException on the observed client. The replacement retained
native capture method `HFPKPTLHCFN.PLWVBUBSAGX`'s monitor/width/JPEG-quality contract.
A live run produced a 1024x576 JPEG from a 2560x1440 display and the development
transport received screen-share event 109. That run did not separately verify
another player's rendered display. At the observed 38 KiB/frame, 10 FPS used roughly 3 Mbps upload
and comparable server egress per viewer; do not treat that estimate as a fixed
bandwidth requirement for other resolutions or quality settings.
