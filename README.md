# Spec Driven RealWorld

Spin up a complete [RealWorld](https://github.com/gothinkster/realworld) backend from a single spec, in one go. Point the tool at [SPEC.md](SPEC.md), let Copilot (or any compatible AI coding tool) generate the implementation into `code/`, then run the provided tests to confirm everything works.

## Description

- A spec-first RealWorld backend contract in [SPEC.md](SPEC.md) with complete API tests ([api.test.mjs](api.test.mjs)).
- Generate a complete, runnable server from the spec in one go into `code/`.
- Sample outputs for various popular stacks under [examples/](examples).

## Prerequisites

- Node 20+ (for `npx` and the test runner)
- Docker (to run Postgres quickly) or a Postgres instance
- An AI coding tool that can read the spec (e.g., Copilot with tool access)

## Quick Start

1. Start Postgres (or point to an existing one):

```bash
docker run -d \
	-e POSTGRES_USER=postgres \
	-e POSTGRES_PASSWORD=password \
	-e POSTGRES_DB=postgres \
	-p 5432:5432 postgres:alpine
export POSTGRES_URI=postgres://postgres:password@localhost:5432/postgres
```

2. Generate the implementation into `code/` using the spec:

```bash
rm -rf code && mkdir code
copilot -p 'Implement SPEC.md' --allow-all-paths --allow-all-tools --allow-all-urls --share ./code/session.md
```

3. Run the full verification loop (build, start, test, clean up):

```bash
./start-and-test.sh
```

If you prefer to run tests manually: `node --test ./api.test.mjs` (server must be running on port 3000).

## Tweak the Tech Stack

- Edit [SPEC.md](SPEC.md) to change the language, framework, or database assumptions (e.g., swap to Go, C#, Rust, etc.).
- Re-run the generation above; the AI coding tool should honor the updated stack and regenerate `code/` accordingly.
- Examples of generated outputs for various stacks live in [examples/](examples) (look for stack-specific folders like `nodejs-express`, `rust-axum`, `dotnet-csharp`, `go-gin` etc).

## How it Works

- The spec defines endpoints, payloads, auth, health check, and infra expectations (port 3000, JWT auth header, encrypted passwords, etc.).
- The AI coding tool consumes the spec and writes all source, assets, and `code/start.sh` without touching anything outside `code/`.
- [start-and-test.sh](start-and-test.sh) launches the generated server, waits for readiness, runs the contract tests in [api.test.mjs](api.test.mjs), and cleans up the port.

## Tips

- If port 3000 is busy, free it before running the tests (the script attempts this automatically).
- Keep `POSTGRES_URI` set; tests assume a reachable database.
- For a quick summary of expected behaviors, run:

```bash
grep -e 'describe("' -e 'it("' -e 'assert(' api.test.mjs
```

Happy spec-driven shipping!
