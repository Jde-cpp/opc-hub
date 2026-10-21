# OPC Hub site

The Angular front end of OPC Hub.

This directory holds:

- `control` - the `jde-opc` Angular library: gateways, servers, nodes and their history.
- `site` - the application that hosts the four libraries.
- `scripts` - `setup.sh` and the other build helpers.

## Building

Run [`scripts/setup.sh`](scripts/setup.sh). It creates `my-workspace` with `ng new`, links the libraries and `site`
into it, and runs `ng build`. Add `--release` to hash the output file names.

`my-workspace` is generated and untracked. Edit the sources under `site` and `<lib>/control`, not the workspace copy.

To serve and test, see [The site](../../README.md#the-site) in the root README.