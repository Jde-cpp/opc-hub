# Self-hosted CI runner

Containerised, auto-registering GitHub Actions runner for the Jde OpcGateway
build. The image adds the clang++-23 / libc++ toolchain to
[`myoung34/github-runner`](https://github.com/myoung34/docker-github-actions-runner);
the 2 GB third-party dependency tree is **bind-mounted from the host**, not baked
in. Consumed by [`.github/workflows/linux-ci.yml`](../workflows/linux-ci.yml).

## Build the image

```bash
sudo docker build -t jde-ci-runner:latest .github/docker
```

The base is pinned in the Dockerfile to `myoung34/github-runner:2.338.2-ubuntu-noble`
(Ubuntu 24.04, glibc 2.39, runner 2.338.2). Base-tag selection has two traps:

- **Distro:** only the `*-ubuntu-noble` tags are noble. The bare version tags
  (`2.336.0`), `latest`, and `ubuntu-focal` are **focal** — glibc 2.31 (< the 2.38
  the mounted host deps need), no `liburing-dev` in apt, and a too-old cmake. Jammy
  (2.35) also fails the glibc floor. Symptom of getting this wrong: `apt` can't find
  `liburing-dev`, or `GLIBC_x.y not found` when the mounted deps load.
- **Runner version:** keep it at what GitHub currently serves. A stale pin is
  not fatal — the image's `CMD` runs the listener under the runner's own
  `run.sh`, whose helper waits for the detached updater's `update.finished` flag
  and relaunches the new listener in place (verified 2026-09-04: a 2.336.0
  container updated to 2.337.0 and was back to `Listening for Jobs` in 80 s with
  no restart) — but every container recreation then self-updates before taking a
  job, and the image no longer matches what runs. Before that `CMD` the update
  **bricked** the container: PID 1 was `bin/Runner.Listener` itself, so its exit
  for the update made `--restart always` restart the container and kill the
  updater mid-copy, after it had already moved `bin` aside; every later start
  exited 127 on a missing `./bin/Runner.Listener`, `config.sh` included, so the
  runner never registered and jobs sat **queued** with no runner (2026-09-03: 631
  restarts, run #70 waiting three hours; recovery = rebuild + `docker rm`, the
  damage being in the writable layer).
  [`runner-version-check.yml`](../workflows/runner-version-check.yml) watches the pin
  weekly on a GitHub-hosted runner (hosted on purpose, so it reports even when the
  self-hosted one is down) and fails, with the rebuild commands in its job summary,
  once GitHub serves a newer runner than the pin. To check by hand, compare the newest
  `<version>-ubuntu-noble` tag on
  [Docker Hub](https://hub.docker.com/r/myoung34/github-runner/tags?name=ubuntu-noble)
  against [`actions/runner`'s latest release](https://github.com/actions/runner/releases/latest)
  and bump the pin (or override without editing):

```bash
sudo docker build --build-arg BASE_IMAGE=myoung34/github-runner:<version>-ubuntu-noble \
  -t jde-ci-runner:latest .github/docker
```

## Run the runner

Replace the placeholder `gha-runner` (registered from the bare image) with the
toolchain image. `ACCESS_TOKEN` is a PAT with `repo` scope (fine-grained: Actions
+ Administration read/write) so the container can fetch its own registration
tokens.

```bash
sudo docker rm -f gha-runner 2>/dev/null || true

sudo docker run -d --restart always --name gha-runner \
  -e REPO_URL="https://github.com/Jde-cpp/opc-hub" \
  -e ACCESS_TOKEN="<PAT>" \
  -e RUNNER_NAME="jde-ci" \
  -e RUNNER_SCOPE="repo" \
  -e EPHEMERAL="1" \
  -e LABELS="linux,container,clang23" \
  -e REPO_DIR="/deps" \
  -v /home/duffyj/code/libs/install:/deps/install:ro \
  -v /home/duffyj/code/libs/UA-Nodeset:/deps/UA-Nodeset:ro \
  --tmpfs /mnt/ram:exec,size=24g \
  --security-opt seccomp=unconfined \
  --cap-add SYS_PTRACE \
  jde-ci-runner:latest
```

The build writes to `/mnt/ram` (per `JDE_BUILD_DIR`) on a tmpfs ramdisk for speed.
Size it above the full debug build (ASan + debug info, ~13 GB) — `24g` — or it hits
`No space left on device` mid-compile; this needs ~24 GB of free host RAM. If RAM
is tight, drop the `--tmpfs` line entirely and the build falls back to the
container's disk-backed overlay (slower, but no RAM cost and effectively unlimited
space).

Why each non-obvious flag:

| Flag | Reason |
|------|--------|
| `-v .../install:/deps/install:ro` | Host-built Boost/protobuf/open62541/… — the presets read `$REPO_DIR/install/clang++/<Debug\|RelWithDebInfo>`. |
| `-v .../UA-Nodeset:/deps/UA-Nodeset:ro` | Clone of [OPCFoundation/UA-Nodeset](https://github.com/OPCFoundation/UA-Nodeset) (clone it to the host path if missing) — `linux-ci.yml` points `UA_NODE_SETS` here for the OpcServer nodeset-load tests. |
| `--tmpfs /mnt/ram:exec,size=24g` | Build output dir on a ramdisk. `size=24g` clears the ~13 GB debug build; `exec` because tmpfs is `noexec` by default and the build runs the binaries/`.so`s it produces. Wiped on restart → clean builds. |
| `--security-opt seccomp=unconfined` | Docker's default seccomp profile blocks `io_uring_setup`; the fwk tests exercise io_uring. |
| `--cap-add SYS_PTRACE` | LeakSanitizer (debug preset builds with ASan/LSan) needs `ptrace`. |
| `EPHEMERAL=1` | Runner deregisters after each job; `--restart always` brings up a fresh registration. Reduces blast radius of a bad job. |

Follow registration with `sudo docker logs -f gha-runner` (look for
`Listening for Jobs`); the runner then appears under repo **Settings → Actions →
Runners**.

**Restarting onto a new image / new token:** prefer a graceful stop so the runner
deregisters its session before exiting —
```bash
sudo docker stop gha-runner && sudo docker rm gha-runner   # SIGTERM: entrypoint deregisters
```
then re-run the block above. A hard `docker rm -f` (SIGKILL) skips that, so the new
container logs `A session for this runner already exists` / `Conflict. Retrying
until reconnected` and loops until GitHub expires the orphaned session (~2 min),
then prints `Runner reconnected` → `Listening for Jobs` on its own. Harmless, just
slower — don't keep recreating the container, which only restarts the clock.

## Notes / trade-offs

- **Clean vs incremental builds:** the `--tmpfs /mnt/ram` is recreated empty when
  the ephemeral container restarts, so each job is a clean build. For incremental
  builds across jobs (faster, less isolation), drop `--tmpfs` for the disk-backed
  overlay or a named volume (`-v jde-build:/mnt/ram`).
- **After a self-update** the container's writable layer holds `bin.<new>` /
  `externals.<new>` beside the image's originals, with `bin` and `externals` as
  symlinks; `docker logs` shows the running `Current runner version`. A recreate
  (`docker rm`) starts again from the image's pinned version and updates again on
  its first start — bump the pin and rebuild to stop paying that.
- **Public repo safety:** self-hosted + public repo means fork PRs must not run
  here — the workflow triggers on `push`/`workflow_dispatch` only. Also keep repo
  **Settings → Actions → General → Fork pull request workflows** at "require
  approval for all outside collaborators".
- **If mounted deps won't load** (ABI/glibc mismatch): rebuild them inside the
  image instead of mounting, using the `linux-clang-debug-repos` preset (needs the
  Boost source and UA-Nodeset too — heavier image, zero host coupling).
