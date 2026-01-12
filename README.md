# Spec Driven RealWorld

A RealWorld backend implementation using spec driven development.

```bash
docker stop $(docker ps -a -q)
docker run -d -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=password -e POSTGRES_DB=postgres -p 5432:5432 postgres:alpine
export POSTGRES_URI=postgres://postgres:password@localhost:5432/postgres
rm -rf code
mkdir code
copilot -p 'Implement SPEC.md' --allow-all-paths --allow-all-tools --allow-all-urls --share ./code/session.md
```
