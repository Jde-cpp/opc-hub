# Code signing policy

Releases up to and including 2026.10.31 are unsigned.  The plan for later releases is to sign the Windows installer with
a [Certum](https://www.certum.eu) Open Source Code Signing certificate issued to the maintainer, so UAC would name the
publisher *Open Source Developer* followed by the maintainer's name.

## What is signed

The Windows installer, `OpcHubSetup-<version>.exe`.  The files inside it ship unsigned:  the project's binaries, its
uninstaller and the third-party DLLs.  `vc_redist.x64.exe` carries Microsoft's own signature.

The installer is built by GitHub Actions from this repository's source, on a GitHub-hosted runner
([`win2025-build.yml`](.github/workflows/win2025-build.yml)), when a release tag is pushed.  The maintainer downloads
that build from the tag's draft release, signs it and uploads it in its place.  Nothing built anywhere else is signed.

## Team roles

| Role | Members |
| --- | --- |
| Committers and reviewers | [Jde-cpp](https://github.com/Jde-cpp) |
| Signer | [Jde-cpp](https://github.com/Jde-cpp) |

Every member uses multi-factor authentication on GitHub.  The signing key stays in Certum's cloud, and each signing
session needs a code from the maintainer's phone.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the
user or the person installing or operating it.

It connects only to the OPC UA servers its operator configures.  If the operator enables Google sign-in, the browser
signs in at accounts.google.com, under [Google's privacy policy](https://policies.google.com/privacy).

## Reporting

Report a signed file you believe was misused, or a security issue, as [SECURITY.md](SECURITY.md) describes.