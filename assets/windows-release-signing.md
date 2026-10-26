# Windows release signing (Azure Artifact Signing)

Windows release binaries are signed with Azure Artifact Signing (formerly Trusted
Signing). Signing keys stay in Azure. GitHub Actions authenticates with OIDC
federated credentials, so there is no PKCS#12 client certificate, API key, or
certificate thumbprint secret.

## Account details

| Field | Value |
| --- | --- |
| Endpoint (West US 2) | `https://wus2.codesigning.azure.net/` |
| Code signing account | `ChiaNetworkInc` |
| Certificate profile | `ChiaNetworkInc` |
| Timestamp | `http://timestamp.acs.microsoft.com` |
| GitHub environment | `windows-code-signing` |

The endpoint must match the account region (West US 2). A mismatch usually
shows up as a 403 / `SignerSign()` failure.

## Release flow

1. `release-policy` determines whether this is a final release and whether macOS
   notarization may run.
2. The `build` job builds and tests Windows and uploads `windows-amd64-binaries`.
3. The `sign-windows` job runs for optimized Windows builds on pushes, releases,
   and manual dispatches. It enters the `windows-code-signing` environment,
   downloads the binaries and staged FTDI driver, assembles the bundle, signs the
   executables and MPIR DLLs, packages and verifies the zip, builds the Inno
   Setup installer from the signed bundle, signs the installer, finalizes the
   signed hash/stable alias, and verifies the installer plus a silent
   install/uninstall smoke test before uploading `windows-amd64-bundle`.
4. On a final release, `publish-release` downloads `windows-amd64-bundle` and
   publishes the versioned zip, `chiavdf-win64.zip`, the versioned setup exe, and
   `chiavdf-win64-setup.exe`.

Pull requests still build and test Windows, but do not enter the signing
environment and do not produce a signed Windows bundle. A PR-only
`test-windows-installer` job compiles the unsigned installer and runs the same
silent install/reinstall/uninstall smoke test with driver staging enabled, so
Inno Setup script, driver, or PATH regressions fail before merge. Optimized
builds on pushes, releases, and manual dispatches sign in `sign-windows`; the
build job uploads only the raw `windows-amd64-binaries` artifact.

## What gets signed

The `sign-windows` job signs the six shipped chiavdf executables:

- `vdf_client.exe`
- `vdf_bench.exe`
- `hw_vdf_client.exe`
- `emu_hw_vdf_client.exe`
- `hw_test.exe`
- `emu_hw_test.exe`

It also signs the MPIR DLLs, including `mpir.dll`. The list comes from
`scripts/package-windows-bundle.ps1 -Action ListSignFiles`. The VC++ 2015-2022
runtime DLLs and the FTDI DLLs keep the signatures they shipped with. The shared
action signs and checks each file; the packaged zip is then extracted and
independently verified before it is uploaded. The package step requires a valid
Authenticode chain for every shipped binary, a timestamp for our signed files,
and a signer subject matching `EXPECTED_SIGNER_SUBJECT` or a thumbprint matching
`EXPECTED_SIGNER_THUMBPRINT`. When neither is set, the default subject must
contain `Chia Network`. If both are set, both must match. Vendor DLLs must match
`EXPECTED_VENDOR_SUBJECT`; the default accepts Microsoft and FTDI subjects. Set
those values from repository variables to pin the publishers exactly.

After the zip is verified, the job builds
`chiavdf-<version>-windows-amd64-setup.exe` with Inno Setup 6.7.3, installed
from the pinned official installer after a SHA-256 check. The installer embeds
the already-signed `bin\` tree and the unmodified FTDI CDM driver package.
`ISCC` receives a `SignTool` through `/S`, so Inno signs the setup exe and, with
`SignedUninstaller=yes`, `unins000.exe` during compilation. There is no
separate installer-signing call. `FinalizeInstaller` then hashes the signed EXE
and creates the stable alias, so neither the sidecar nor the alias predates the
signature. `VerifyInstaller` checks the checksum, Authenticode chain, timestamp,
and expected signer. `TestInstaller` runs it silently with `/DRIVER=1` in a
directory under `%ProgramFiles%`, accepts exit codes `0` and `3010`, checks all
six executables, confirms `pnputil /enum-drivers` lists the driver, reinstalls
to confirm the `PATH` entry appears exactly once, then runs the uninstaller.
The binary signing list is 15 files.

## How auth works

The `sign-windows` job in `.github/workflows/build-client.yml`:

- Declares `environment: windows-code-signing` and `permissions.id-token: write`.
- Assumes `AZURE_SIGNING_CLIENT_ID` and `AZURE_SIGNING_TENANT_ID` are configured
  on that environment; the shared signing action fails if either is missing.
- Installs the pinned Inno Setup 6.7.3 release before any step that can request
  an Azure/OIDC token.
- Installs the signing client with the pinned
  `Chia-Network/actions/sign/azure-windows` action and `sign: 'false'`, then
  lists the files to sign and calls the same action with the file list and
  secrets.
- After packaging the zip, builds the installer; ISCC signs the setup exe and
  its uninstaller during compilation through `INSTALLER_SIGNTOOL`.
- The shared action installs `Microsoft.ArtifactSigning.Client` from NuGet,
  writes `metadata.json`, and adds the Windows App Certification Kit to `PATH`
  for `signtool.exe`.
- Immediately before each signing loop, the action requests a fresh GitHub OIDC
  token with `audience=api://AzureADTokenExchange` and writes it to
  `AZURE_FEDERATED_TOKEN_FILE`. Requesting it late avoids an expired token after
  a long build. The token is reused for the file list, so keep the list and
  Azure response time within the token lifetime; the current binary list is 15
  files.
- The action sets `AZURE_TOKEN_CREDENTIALS=prod`, then maps `AZURE_TENANT_ID` and
  `AZURE_CLIENT_ID` from the two environment secrets. Azure Identity uses the
  federated token file; it does not fall back to Azure CLI credentials.
- It runs `signtool sign /fd SHA256 /tr http://timestamp.acs.microsoft.com
  /td SHA256 /dlib ... /dmdf ... FILE` and fails the job if signing or
  verification fails. The dlib requires the .NET 8 runtime, which
  `windows-latest` provides.

## Manual packaging

The script exposes these actions:

- `Assemble` requires `INSTALLER_VERSION` and builds `dist/windows/<bundle>/bin`.
- `ListSignFiles` requires `INSTALLER_VERSION` and prints the absolute paths of
  the six executables and MPIR DLLs.
- `Package` requires `INSTALLER_VERSION`, `IS_RELEASE`, and `REQUIRE_SIGNATURES`.
  Set `REQUIRE_SIGNATURES=false` for an unsigned local package; set it to `true`
  only after signing, and optionally set `EXPECTED_SIGNER_SUBJECT`,
  `EXPECTED_SIGNER_THUMBPRINT`, or `EXPECTED_VENDOR_SUBJECT` to pin the
  publishers.
- `BuildInstaller` requires `INSTALLER_VERSION` and `REQUIRE_SIGNATURES`, plus
  Inno Setup 6, the signed bundle, and `src/hw/libft4222/driver`. When
  `REQUIRE_SIGNATURES=true` it also requires `INSTALLER_SIGNTOOL`, the command
  passed to `ISCC` with `/SChiaSignTool=...`, so Inno signs the setup exe and,
  with `SignedUninstaller=yes`, `unins000.exe` during compilation. It writes
  the signed versioned setup exe.
- `FinalizeInstaller` requires `INSTALLER_VERSION`, `IS_RELEASE`, and
  `REQUIRE_SIGNATURES`; after signing it verifies the signature, writes the
  `.sha256` from the signed EXE, and creates the stable
  `chiavdf-win64-setup.exe` alias on release.
- `VerifyInstaller` requires `INSTALLER_VERSION`, `IS_RELEASE`, and
  `REQUIRE_SIGNATURES`; it checks the sidecar, signature, and release alias.
- `TestInstaller` requires `INSTALLER_VERSION` and performs the silent
  install/reinstall/version/uninstall smoke test with `/DRIVER=1`.

The `Sign` action is intentionally not local: it is provided by the pinned
`Chia-Network/actions/sign/azure-windows` commit and needs GitHub OIDC
variables. The installer SignTool helper in `scripts/sign-windows-file.ps1`
uses the same client and flags. It only signs files under the workspace `dist`
directory or the Inno Setup temporary directory, so an unexpected caller
cannot sign arbitrary files.

`scripts/get-libft4222.ps1 install` stages the FTDI CDM files under
`src/hw/libft4222/driver`. `BuildInstaller` refuses to run if `ftdibus.inf`,
`ftdibus.cat`, `ftdiport.inf`, `ftdiport.cat`, the amd64/i386 payloads, or the
FT4222H VID/PID declaration are missing. The catalog signer must be the exact
common name `Microsoft Windows Hardware Compatibility Publisher` by default;
set `EXPECTED_DRIVER_SIGNER_CN` to override it. The file list, VID/PID check,
and catalog signer check live in `scripts/windows-ftdi-driver.ps1` and are
shared with the staging script.

## GitHub secrets (environment `windows-code-signing`)

| Secret | Purpose |
| --- | --- |
| `AZURE_SIGNING_CLIENT_ID` | Entra app (client) ID. |
| `AZURE_SIGNING_TENANT_ID` | Entra tenant ID. |

No client certificate, API key, or certificate thumbprint secrets are used. The
old DigiCert `SM_*` secrets are no longer read and can be removed.

## Enable signing on a new repository

1. In Entra ID, add a federated credential to the existing signing app for the
   GitHub repository. The subject must include the environment. For an existing
   repo that has not opted into immutable subject claims, use
   `repo:Chia-Network/chiavdf:environment:windows-code-signing`. Repos created
   after July 15, 2026, or repos that opt in, use the immutable form
   `repo:OWNER@OWNER_ID/REPO@REPO_ID:environment:windows-code-signing` instead.
   The Entra wizard may generate the immutable form, so edit the subject if the
   repo still emits the name-based format. Look under **Home > App Registrations
   > All applications > GithubActions > Certificates & secrets > Federated
   credentials**.
2. Confirm the app has the **Artifact Signing Certificate Profile Signer** role
   on the `ChiaNetworkInc` account/profile. The federated credential issuer is
   `https://token.actions.githubusercontent.com` and the audience is
   `api://AzureADTokenExchange`.
3. In the GitHub repository, create the `windows-code-signing` environment and
   add `AZURE_SIGNING_CLIENT_ID` and `AZURE_SIGNING_TENANT_ID` (the same values as
   the other Chia repositories unless a separate app is used).
4. Optionally restrict the environment to the branches and tags that should
   produce signed builds. This repo uses numeric release tags such as `1.2.0`,
   not `v1.2.0`, so a `v*` tag rule blocks releases. If you add tag rules, use a
   pattern that matches the numeric tags, such as `1.*` or `*`. A published
   release runs on a tag ref, so a branch-only rule can block it. Signing runs
   for optimized Windows builds on pushes, releases, and manual dispatches, so
   make sure the environment allows those branches and tags.
5. Copy the `build-client.yml` signing pattern: a dedicated Windows job with the
   environment and `id-token: write`, using the pinned
   `Chia-Network/actions/sign/azure-windows` commit for the NuGet client, late
   OIDC token, `signtool` with `/dlib` and `/dmdf`, and `signtool verify /pa`.

## Testing before enabling releases

1. Run **Actions → VDF client & HW CI → Run workflow**. The optimized Windows
   build signs on every run, including this manual dispatch, without publishing
   a GitHub release.
2. Download the `windows-amd64-bundle` artifact.
3. Unzip it and verify a signed binary and the installer:

   ```powershell
   Expand-Archive .\chiavdf-<version>-windows-amd64.zip -DestinationPath .\zip
   signtool verify /v /pa .\zip\chiavdf-<version>-windows-amd64\bin\vdf_client.exe
   signtool verify /v /pa .\chiavdf-<version>-windows-amd64-setup.exe
   ```

   `chiavdf-win64-setup.exe` is the stable alias produced only when
   `IS_RELEASE=true`. The workflow already runs the silent install/uninstall
   smoke test; doing it manually is useful when testing a new Inno Setup version
   or driver package.

To test from a pull-request branch before merging, allow that branch in the
environment's deployment rules, or merge and dispatch the workflow from `main`.

## Troubleshooting

- **Missing credentials:** the shared signing action fails when it cannot see
  `AZURE_SIGNING_CLIENT_ID` or `AZURE_SIGNING_TENANT_ID`. Check the environment
  name, the federated credential subject, and repository access.
- **OIDC / federated credential errors:** the repository or environment in Entra
  does not match, or `permissions.id-token: write` is missing.
- **403 / `SignerSign()` failure:** the endpoint region does not match the
  account, or the app lacks Artifact Signing permissions on the account/profile.
- **Token expired mid-job:** the shared action requests one token before the
  signing loop. If the file list grows, request a token per file or per batch in
  the shared action rather than relying on the late single request.
- **`ISCC.exe` not found:** use the pinned `install-inno-setup` action on the
  runner or set `INNO_SETUP_ISCC` to the compiler path.
- **Missing FTDI driver inputs:** the build job must upload
  `src/hw/libft4222/driver/**`; run `scripts/get-libft4222.ps1 install` there.
  `BuildInstaller` checks the INF/CAT files, the amd64/i386 payloads, and the
  catalog signatures before compiling.
- **`pnputil` exit code 3010:** this is success with a pending reboot. The
  installer returns 3010, and the winget manifest must list 3010 in
  `InstallerSuccessCodes`. A `pnputil` return code of `259` is handled normally
  by setup because it means the package was staged but no device matched or a
  newer driver is already active; setup itself does not exit with `259`. Any
  other nonzero `pnputil` code fails setup; `[Code]` logs it, shows the failure
  on the finish page for interactive installs, and returns exit code `1603`
  after the files and PATH entry have been installed.
- **Installer smoke test skipped:** `TestInstaller` skips when the PowerShell
  process is not elevated and CI is not set. In CI it fails instead of
  skipping, because a skipped test would hide installer regressions. A local
  non-elevated shell should test on a disposable Windows VM instead.
