# Code signing policy

Free code signing provided by [SignPath.io](https://signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## What is signed

The Windows installer, `OpcHubSetup-<version>.exe`, its uninstaller, and the project's own binaries inside it:
`Jde.Opc.Hub.exe`, `Jde.Opc.Server.exe`, `Jde.CheckCpu.exe` and the `Jde.*.dll` files.  Third-party DLLs ship
unsigned inside the signed installer; `vc_redist.x64.exe` carries Microsoft's own signature.

Every signed file is built by GitHub Actions from this repository's source, on a GitHub-hosted runner
([`win2025-build.yml`](.github/workflows/win2025-build.yml)), when a release tag is pushed.  Each signing request is
approved by hand.

## Team roles

| Role | Members |
| --- | --- |
| Committers and reviewers | [Jde-cpp](https://github.com/Jde-cpp) |
| Approvers | [Jde-cpp](https://github.com/Jde-cpp) |

Every member uses multi-factor authentication on GitHub and SignPath.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the
user or the person installing or operating it.

It connects only to the OPC UA servers its operator configures.  If the operator enables Google sign-in, the browser
signs in at accounts.google.com, under [Google's privacy policy](https://policies.google.com/privacy).

## Reporting

Report a signed file you believe was misused, or a security issue, as [SECURITY.md](SECURITY.md) describes.