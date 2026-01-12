# realworld-spec-driven

A RealWorld backend implementation using spec driven development.

# Setup

Ensure Postgres is available and provided via env var `POSTGRES_URI`. For example, 
```
docker run -d -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=password -e POSTGRES_DB=postgres -p 5432:5432 postgres:alpine
export POSTGRES_URI=postgres://postgres:password@localhost:5432/postgres
```

# Generate Code

```
copilot -p 'Implement SPEC.md' --allow-all-paths --allow-all-tools --allow-all-urls
```