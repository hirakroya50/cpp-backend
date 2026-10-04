# C++ Neon CRUD backend

Drogon HTTP server + libpq PostgreSQL client + Swagger UI.
Uses the existing public.users and public.products tables; creates no tables and changes no schema.

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
- This is a local development starter, bound to 127.0.0.1. Authentication, authorization, request tracing, rate limits, TLS ingress, and deployment/CI integration must be added before exposing user data remotely.
- Swagger does not implement routes; changes to routes or validation must also be reflected in openapi.json.

## Verification

OpenAPI JSON syntax, internal schema references, and endpoint coverage were checked during creation. Compilation and live Neon CRUD were not run in the creation environment because Drogon/libpq and database credentials were unavailable. Run the included smoke test against your local server and a development Neon branch to verify actual persistence and errors.

```bash
python3 smoke_test.py
```

The test creates temporary user/product records, reads/lists/updates/deletes them, and checks duplicate-email and validation errors. It performs real writes; use a development database. If interrupted, records with smoke-test names may remain.
