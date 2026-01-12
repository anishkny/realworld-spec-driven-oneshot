# Introduction
Backend API for Medium.com like blogging platform.

Supports users, articles, favorites, follows and feed.

Output all source code in src/ and test code in test/

Entrypoint should be src/index.js

Use PORT 3000.

# Tech stack
* Node.js
* Express
* Postgres (provided via env var POSTGRES_URI, default to postgres://postgres:password@localhost:5432/postgres)

# Tests
Write API level integration tests in mocha in test/api.test.js

Run tests with `npm run start-and-test` which will start server and run API tests.
