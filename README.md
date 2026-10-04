# C++ Neon CRUD backend

Drogon HTTP server + libpq PostgreSQL client + Swagger UI.
Uses the existing public.users and public.products tables; creates no tables and changes no schema.

## Run with Docker Compose

Docker Desktop (Mac/Windows) or Docker Engine with the Compose plugin (Linux) is required.
Keep your existing `.env`, or copy `.env.example` to `.env` and supply your Neon URI.
Compose loads `.env` automatically. Credentials are supplied at runtime and excluded from
the image and build context. No local PostgreSQL container is needed.

```bash
docker compose up -d --build
docker compose ps
curl -i http://localhost:8080/health
curl -i http://localhost:8080/ready
```

Swagger: http://localhost:8080/docs. If port 8080 is occupied, add `HTTP_PORT=8081`
to `.env` and use port 8081 in these URLs.

```bash
docker compose logs -f backend
docker compose down
```

The image uses a multi-stage Alpine build, static Drogon/Trantor libraries, a
size-optimized stripped executable, and only required runtime packages. Compilers,
source, Git, and development headers remain in the builder. The runtime runs as
UID 10001 with a read-only filesystem. BusyBox's included `wget` provides the health
check without installing curl. Drogon's temporary upload directories use `/tmp`
via `UPLOAD_PATH=/tmp/uploads`. `/health` checks the process; `/ready` checks Neon.

For deployment, copy the project to a Docker host, configure `.env`, and run the
same Compose command. By default, the published port binds to the host's loopback
interface. Place a reverse proxy on that host in front of it for HTTPS. Set
`BIND_ADDRESS` in `.env` to a specific host interface or `0.0.0.0` when you need
external access. The API currently has no authentication; add access control before
exposing its user data publicly.

Build for the deployment host's architecture, or build and push a multi-platform
image to your own registry (replace the example registry name):

```bash
docker buildx build --platform linux/amd64,linux/arm64 \
  -t YOUR_REGISTRY/cpp-backend:latest --push .
```

To run that published image, change `image:` in `compose.yaml` to the registry tag
and use `docker compose pull && docker compose up -d --no-build`.
Inspect local image size with `docker image inspect cpp-backend:local --format '{{.Size}}'`
(bytes, uncompressed). Rebuild periodically with `docker compose build --pull --no-cache`
to pick up Alpine runtime updates.

## 1. Install dependencies on your Mac

```bash
brew install cmake drogon libpq watchexec
```

This project links libpq independently, so rebuilding Drogon for PostgreSQL support is unnecessary.

## 2. Configure Neon locally

In the Neon dashboard, select the correct project, branch, database, and role, then click Connect.
Copy the PostgreSQL connection URI. Keep its SSL parameters. A direct URI is fine for this four-connection pool; a pooled URI also works.

Create `.env` in this project folder:

```bash
export DATABASE_URL='postgresql://YOUR_USER:YOUR_PASSWORD@YOUR_NEON_HOST/YOUR_DATABASE?sslmode=require&channel_binding=require'
```

Use the exact URI Neon supplies, including URL-encoded credentials. Do not paste the URI into source code, Swagger, Git, or a chat. `.env` is ignored by Git. This application does not automatically load `.env`; source it before running.

```bash
source .env
```

## 3. Build (run from the project folder)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPostgreSQL_ROOT="$(brew --prefix libpq)"
cmake --build build -j 2
./build/cpp_backend
```

Stop any other server using port 8080 first.
If you copied these files over the previous project and CMake has stale settings, remove only its generated `build` directory and configure again.

## 4. Verify the database

Open http://localhost:8080/ready or run:

```bash
curl -i http://localhost:8080/ready
```

200 and `{"database":"ok"}` means a query reached PostgreSQL. `/health` checks the HTTP process only.
A cold Neon compute may require a retry. Persistent 503: check the URI, branch, role, network access, and whether the role has permission on both tables and their BIGSERIAL sequences.

## 5. Open Swagger

http://localhost:8080/docs

Swagger loads `/openapi.json`. Expand an endpoint, click Try it out, edit the body, and click Execute. Swagger's CSS and JavaScript use a pinned public CDN, so loading the UI needs internet access.

Create a user:

```json
{"name":"Hirak Roy","email":"hirak@example.com","phone":null}
```

Create a product:

```json
{"name":"Keyboard","description":"USB keyboard","price":"1999.00","stock":10}
```

Copy each returned id, then use GET, PUT, and DELETE on that record. PUT replaces all editable fields: required fields must be supplied, omitted phone/description become null, and omitted stock becomes zero. There is no PATCH route.

Price is an exact decimal STRING to avoid floating-point money rounding. BIGINT IDs are also strings, avoiding JavaScript's integer precision limit. Stock is a JSON integer.

## Routes

| Method | Users | Products |
|---|---|---|
| POST | /api/v1/users | /api/v1/products |
| GET | /api/v1/users | /api/v1/products |
| GET | /api/v1/users/{id} | /api/v1/products/{id} |
| PUT | /api/v1/users/{id} | /api/v1/products/{id} |
| DELETE | /api/v1/users/{id} | /api/v1/products/{id} |

Lists support `?limit=20&offset=0` (maximum limit 100).
Create: 201; reads/updates: 200; delete: 204; invalid input: 400; missing record: 404; duplicate email: 409; overload/database failure: 503.

## Auto rebuild during development

After sourcing `.env` and configuring CMake once:

```bash
watchexec --restart --watch src --watch public --watch CMakeLists.txt --debounce 500ms --shell bash -- 'cmake --build build -j 2 && exec ./build/cpp_backend'
```

Saving source recompiles and restarts; saving the JSON or docs restarts so their contents reload. Changes to `.env` require re-sourcing it and restarting the watcher.

## Design and limits

- Four database workers, each exclusively owning one connection; SQL does not block HTTP event-loop threads.
- Bounded pending queue, parameterized SQL, RAII cleanup, input validation, server-side query timeout, and connection timeout.
- Failed connections reconnect on the next request. Writes are never automatically retried: a network failure can leave the commit outcome uncertain.
- `users.updated_at` is set explicitly on PUT; the schema's DEFAULT alone does not update it. Products have no updated_at column, so none is assumed.
- Emails retain your database's existing case-sensitive uniqueness behavior.
- Timestamps are PostgreSQL text; names/strings have byte-length validation. Email validation is a basic format check, not verification.
- Native runs bind to 127.0.0.1 by default; `LISTEN_HOST` overrides this, and the Docker image sets it to 0.0.0.0. Authentication, authorization, request tracing, rate limits, TLS ingress, and CI integration must be added before exposing user data remotely.
- Swagger does not implement routes; changes to routes or validation must also be reflected in openapi.json.

## Verification

OpenAPI JSON syntax, internal schema references, and endpoint coverage were checked during creation. Compilation and live Neon CRUD were not run in the creation environment because Drogon/libpq and database credentials were unavailable. Run the included smoke test against your local server and a development Neon branch to verify actual persistence and errors.

```bash
python3 smoke_test.py
```

The test creates temporary user/product records, reads/lists/updates/deletes them, and checks duplicate-email and validation errors. It performs real writes; use a development database. If interrupted, records with smoke-test names may remain.
