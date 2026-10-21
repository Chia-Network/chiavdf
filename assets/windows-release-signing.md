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

1. `release-policy` sets `sign_windows=true` for a final release, or for a manual
   `workflow_dispatch` with `windows_sign` enabled.
2. The `build` job builds and tests Windows, uploads
   `binaries-optimized=1-windows-latest`, and skips Windows packaging because
   signing was requested.
3. The `sign-windows` job enters the `windows-code-signing` environment,
   downloads the binaries, assembles the bundle, signs it, packages the zip, and
   verifies every signature and every executable before uploading
   `windows-amd64-bundle`.
4. `publish-release` downloads `windows-amd64-bundle` and publishes the
   versioned zip plus `chiavdf-win64.zip`.

Pull-request and push builds have `sign_windows=false`, so they stay entirely in
the `build` job and publish only unsigned workflow artifacts.

## What gets signed

`scripts/package-windows-bundle.ps1` signs the six shipped chiavdf executables:

- `vdf_client.exe`
- `vdf_bench.exe`
- `hw_vdf_client.exe`
- `emu_hw_vdf_client.exe`
- `hw_test.exe`
- `emu_hw_test.exe`

It also signs the MPIR DLLs, including `mpir.dll`. The VC++ 2015-2022 runtime
DLLs and the FTDI DLLs keep the signatures they shipped with. After signing,
`signtool verify /pa` runs on every binary and the packaged zip is extracted and
verified again before it is uploaded.

## How auth works

The `sign-windows` job in `.github/workflows/build-client.yml`:

- Declares `environment: windows-code-signing` and `permissions.id-token: write`.
- Checks for `secrets.AZURE_SIGNING_CLIENT_ID` and
  `secrets.AZURE_SIGNING_TENANT_ID`; signing is not silently skipped when they
  are missing.
- Runs the `Sign` action in `scripts/package-windows-bundle.ps1`, which installs
  `Microsoft.ArtifactSigning.Client` from NuGet, writes `metadata.json`, and adds
  the Windows App Certification Kit to `PATH` for `signtool.exe`.
- Immediately before `signtool`, requests a fresh GitHub OIDC token with
  `audience=api://AzureADTokenExchange` and writes it to
  `AZURE_FEDERATED_TOKEN_FILE`. Requesting it late avoids an expired token after
  a long build.
- Sets `AZURE_TOKEN_CREDENTIALS=prod`, then maps `AZURE_TENANT_ID` and
  `AZURE_CLIENT_ID` from the two environment secrets. Azure Identity uses the
  federated token file; it does not fall back to Azure CLI credentials.
- Runs `signtool sign /v /fd SHA256 /tr http://timestamp.acs.microsoft.com
  /td SHA256 /dlib ... /dmdf ... FILE` and fails the job if signing or
  verification fails.

Unsigned pull-request and push builds never enter the environment. They assemble
and package the zip in the normal build job with signing disabled.

## GitHub secrets (environment `windows-code-signing`)

| Secret | Purpose |
| --- | --- |
| `AZURE_SIGNING_CLIENT_ID` | Entra app (client) ID. |
| `AZURE_SIGNING_TENANT_ID` | Entra tenant ID. |

No client certificate, API key, or certificate thumbprint secrets are used. The
old DigiCert `SM_*` secrets are no longer read and can be removed.

## Enable signing on a new repository

1. In Entra ID, add a federated credential to the existing signing app for the
   GitHub repository. The subject must include the environment, for example
   `repo:Chia-Network/chiavdf:environment:windows-code-signing`. Look under
   **Home > App Registrations > All applications > GithubActions > Add a
   Certificate or Secret**.
2. Confirm the app can use Artifact Signing account/profile `ChiaNetworkInc`.
3. In the GitHub repository, create the `windows-code-signing` environment and
   add `AZURE_SIGNING_CLIENT_ID` and `AZURE_SIGNING_TENANT_ID` (the same values as
   the other Chia repositories unless a separate app is used).
4. Optionally restrict the environment to the branches and tags that should
   produce signed builds. This repo uses numeric release tags such as `1.2.0`,
   not `v1.2.0`, so a `v*` tag rule blocks releases. If you add tag rules, use a
   pattern that matches the numeric tags, such as `1.*` or `*`. A published
   release runs on a tag ref, so a branch-only rule can block it. Signing jobs
   run only for final releases or a manual `windows_sign` dispatch, so pull
   requests are unaffected.
5. Copy the `build-client.yml` signing pattern: a dedicated Windows job with the
   environment and `id-token: write`, calling a signing action that handles the
   NuGet client, the late OIDC token request, `signtool` with `/dlib` and
   `/dmdf`, and `signtool verify /pa`.

## Testing before enabling releases

1. Run **Actions → VDF client & HW CI → Run workflow** with `windows_sign`
   enabled. This is a `workflow_dispatch` run, so it signs without publishing a
   GitHub release.
2. Download the `windows-amd64-bundle` artifact.
3. Unzip it and verify a signed binary:

   ```powershell
   signtool verify /v /pa .\bin\vdf_client.exe
   ```

To test from a pull-request branch before merging, allow that branch in the
environment's deployment rules, or merge and dispatch the workflow from `main`.

## Troubleshooting

- **Unsigned or skipped:** the job cannot see `AZURE_SIGNING_CLIENT_ID`. Check
  the environment name, the federated credential subject, and repository access.
- **OIDC / federated credential errors:** the repository or environment in Entra
  does not match, or `permissions.id-token: write` is missing.
- **403 / `SignerSign()` failure:** the endpoint region does not match the
  account, or the app lacks Artifact Signing permissions on the account/profile.
- **Token expired mid-job:** request the federated token immediately before
  `signtool`, not at the start of packaging.
