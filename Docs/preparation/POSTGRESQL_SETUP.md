# PostgreSQL setup for DeckKernel tests

[TOC|PostgreSQL setup]

This guide prepares a local PostgreSQL installation on Windows for the DeckKernel
functional tests. The application uses the dedicated PostgreSQL role
`deckkernel_user` and authenticates it through Windows SSPI without sending a
database password.

The first connection test itself is documented in:

[FIRST_CONNECTION_TEST.md](../build/FIRST_CONNECTION_TEST.md)

## 1. Administrative PostgreSQL connection

Open pgAdmin or `psql` using the PostgreSQL administrator account, normally
`postgres`.

Verify the effective identity:

```sql
SELECT current_user, session_user;
```

For administrative work both should normally be `postgres`.

If a pgAdmin connection has switched the effective role, restore it with:

```sql
RESET ROLE;
```

## 2. Create the DeckKernel database

Create the database once:

```sql
CREATE DATABASE "DeckKernel";
```

`CREATE DATABASE` must not run inside an explicit transaction block.

Reconnect the Query Tool to database `DeckKernel`.

PostgreSQL schemas are local to one database. Creating `deckkernel_test` while the
Query Tool is still connected to the default `postgres` database does not create that
schema in `DeckKernel`.

Before executing any DeckKernel setup script, verify the active database:

```sql
SELECT current_database();
```

The result must be:

```text
DeckKernel
```

If it is not, change the pgAdmin Query Tool connection to `DeckKernel` or reconnect
`psql` with `-d DeckKernel` before continuing.

## 3. Create the application role and test schema

The repository contains the executable setup script:

`test/postgresql_sspi_setup.sql`

Before executing the script, verify again that the current connection is using
`DeckKernel`:

```sql
SELECT current_database();
```

Only then execute the file as a PostgreSQL administrator in pgAdmin or `psql`.
The script contains its own guard and aborts if it is run against another database.
The script sets up **both** the disposable model schema \`deckkernel_test\`
and the permanent image cache schema \`deckkernel_cache\`. The cache table is
created here, **not from FirstConnection**. The relevant cache portion is:

```sql
CREATE SCHEMA IF NOT EXISTS deckkernel_cache AUTHORIZATION deckkernel_user;
ALTER SCHEMA deckkernel_cache OWNER TO deckkernel_user;
GRANT USAGE, CREATE ON SCHEMA deckkernel_cache TO deckkernel_user;

CREATE TABLE IF NOT EXISTS deckkernel_cache.scryfall_images (
   url text PRIMARY KEY,
   image_data bytea NOT NULL,
   sha256 text NOT NULL CHECK (length(sha256) = 64),
   media_type text NOT NULL,
   cached_at timestamptz NOT NULL DEFAULT now(),
   CHECK (octet_length(image_data) > 0)
);

ALTER TABLE deckkernel_cache.scryfall_images OWNER TO deckkernel_user;

GRANT SELECT, INSERT, UPDATE, DELETE
   ON TABLE deckkernel_cache.scryfall_images TO deckkernel_user;

```

Run the complete checked-in SQL file from the DeckKernel repository root,
as a PostgreSQL administrator while connected to database \`DeckKernel\`:

```cmd
psql -U postgres -d DeckKernel -v ON_ERROR_STOP=1 -f test/postgresql_sspi_setup.sql
```

Alternatively, execute the same SQL file in pgAdmin's Query Tool using
an administrative connection to \`DeckKernel\`.

The file is **idempotent** for the existing cache: it creates missing schema
and table objects, repairs ownership, and grants the necessary rights without
deleting image bytes or replacing their URL keys.

**Do not grant database-wide \`CREATE\` to \`deckkernel_user\`.** PostgreSQL
requires that privilege even for \`CREATE SCHEMA IF NOT EXISTS\`. FirstConnection
now checks the pre-existing cache schema and table and fails with a setup
instruction if they are missing or not accessible. It does not execute any
\`CREATE SCHEMA\` or \`CREATE TABLE\` statements in \`deckkernel_cache\`.

The `$do$ ... $do$` pairs are PostgreSQL dollar-quote delimiters. Both delimiters are
required; a single `$` is not valid syntax.

The script:

- verifies that the script is running in database `DeckKernel`;
- creates `deckkernel_user LOGIN` if necessary and ensures an existing role has LOGIN;
- grants it connection access to `DeckKernel`;
- creates and assigns ownership of schema `deckkernel_test`;
- creates the persistent `deckkernel_cache` schema and URL-keyed
  `scryfall_images` table (or preserves existing image data);
- grants schema usage/create rights, and read/write permissions on the image cache;
- repairs ownership of existing first-connect test tables when they were previously
  created by an administrator.

The application role intentionally remains a normal non-administrative role.

## 4. Locate the active PostgreSQL configuration files

Run:

```sql
SHOW hba_file;
SHOW ident_file;
SHOW data_directory;
```

Edit the files reported by PostgreSQL. Do not infer their locations from the
installation directory.

## 5. Determine the SSPI mapping identity

Do not copy the output of `whoami` directly into `pg_ident.conf`.

With the SSPI options used below, PostgreSQL keeps the realm
(`include_realm=1`), uses the SAM-compatible user name
(`upn_username=0`) and the SAM-compatible/NetBIOS realm
(`compat_realm=1`). The name passed to the PostgreSQL user map therefore has
the form:

```text
username@DOMAIN
```

This is different from the usual `whoami` display form:

```text
DOMAIN\username
```

In the Windows account that will run DeckKernel, inspect both components:

```cmd
echo %USERNAME%@%USERDOMAIN%
```

For example, if `USERNAME=vhillmann` and `USERDOMAIN=ADECC`, the SSPI mapping
identity is:

```text
vhillmann@ADECC
```

The `system_user` value shown after a successful connection can still use a
different presentation such as `sspi:ADECC\vhillmann`. That display value is
not the literal `pg_ident.conf` mapping key.

## 6. Configure pg_ident.conf

Add a dedicated mapping from the SSPI principal to the PostgreSQL role:

```text
# MAPNAME           SYSTEM-USERNAME       PG-USERNAME
deckkernel_sspi     username@DOMAIN       deckkernel_user
```

Replace `username@DOMAIN` with the value determined in section 5. For example:

```text
deckkernel_sspi     vhillmann@ADECC        deckkernel_user
```

The map means that this authenticated Windows identity is allowed to connect as the
PostgreSQL role `deckkernel_user`.

## 7. Configure pg_hba.conf

Add the DeckKernel-specific SSPI rules before broader localhost rules such as
`scram-sha-256`:

```text
# TYPE  DATABASE    USER               ADDRESS          METHOD  OPTIONS
host    DeckKernel  deckkernel_user    127.0.0.1/32     sspi    include_realm=1 compat_realm=1 upn_username=0 map=deckkernel_sspi
host    DeckKernel  deckkernel_user    ::1/128          sspi    include_realm=1 compat_realm=1 upn_username=0 map=deckkernel_sspi
```

A typical resulting order is:

```text
host    DeckKernel  deckkernel_user    127.0.0.1/32     sspi    include_realm=1 compat_realm=1 upn_username=0 map=deckkernel_sspi
host    DeckKernel  deckkernel_user    ::1/128          sspi    include_realm=1 compat_realm=1 upn_username=0 map=deckkernel_sspi
host    all         all                127.0.0.1/32     scram-sha-256
host    all         all                ::1/128          scram-sha-256
```

PostgreSQL uses the first matching HBA rule.

## 8. Reload and validate the configuration

Run as administrator:

```sql
SELECT pg_reload_conf();

SELECT
   line_number,
   type,
   database,
   user_name,
   address,
   auth_method,
   options,
   error
FROM pg_hba_file_rules
ORDER BY line_number;

SELECT
   map_number,
   file_name,
   line_number,
   map_name,
   sys_name,
   pg_username,
   error
FROM pg_ident_file_mappings
ORDER BY line_number;
```

The DeckKernel HBA rules and identity mapping should have `error IS NULL`.
These views validate the configuration files and their syntax. They do not prove that
the actual Windows principal produced by SSPI matches the `sys_name` in the map.

## 9. Test SSPI independently with psql

Use the PostgreSQL command-line client before testing DeckKernel:

```bat
set PGPASSWORD=&& "C:\Program Files\PostgreSQL\18\bin\psql.exe" -w -h localhost -p 5432 -d DeckKernel -U deckkernel_user
```

The command first removes any `PGPASSWORD` value and uses `-w` so psql is not
allowed to prompt for or silently fall back to an interactive password. A successful
connection therefore proves that password authentication was not required.

Inside `psql` run:

```sql
SELECT
   current_database,
   current_user,
   session_user,
   system_user
FROM (
   SELECT
      current_database() AS current_database,
      current_user,
      session_user,
      system_user
) AS connection_identity;
```

A successful integrated connection is expected to show:

```text
current_database = DeckKernel
current_user      = deckkernel_user
session_user      = deckkernel_user
system_user       = sspi:DOMAIN\username
```

## 10. Distinguish HBA and SSPI mapping failures

The failure mode tells you which part of the configuration was reached:

- If psql asks for a password when run without `-w`, an SSPI HBA rule did not match
  and a later password rule such as `scram-sha-256` was selected.
- If the SSPI HBA rule matches but the Windows identity does not match the
  `pg_ident.conf` entry, PostgreSQL reports an SSPI/user-map authentication failure;
  it does not continue to the later password rule.
- PostgreSQL uses only the first matching `pg_hba.conf` record. Put both DeckKernel
  SSPI records before the general localhost password records.
- After every change to `pg_hba.conf` or `pg_ident.conf`, run
  `SELECT pg_reload_conf();`.

Use the `-w` psql command from section 9 while diagnosing SSPI. It prevents a password
prompt from hiding the fact that the wrong HBA rule was selected.

## 11. Verify ownership of existing test objects

The cache table must be owned by `deckkernel_user`. Verify it independently
before starting FirstConnection:

```sql
SELECT current_database(),
       has_schema_privilege('deckkernel_user', 'deckkernel_cache', 'USAGE') AS cache_schema_usage,
       has_table_privilege('deckkernel_user', 'deckkernel_cache.scryfall_images', 'SELECT') AS cache_read,
       has_table_privilege('deckkernel_user', 'deckkernel_cache.scryfall_images', 'INSERT') AS cache_insert,
       has_table_privilege('deckkernel_user', 'deckkernel_cache.scryfall_images', 'UPDATE') AS cache_update;
```

All privilege columns must be `true`. If the schema/table is missing, rerun
`test/postgresql_sspi_setup.sql` as administrator first. Never solve that
failure by granting `CREATE ON DATABASE` to the normal application role.


If the test objects already exist, verify their owners:

```sql
SELECT
   schemaname,
   tablename,
   tableowner
FROM pg_tables
WHERE schemaname = 'deckkernel_test'
ORDER BY tablename;
```

For the first-connect tables, the owner should be `deckkernel_user`.

If not, rerun the SQL block from section 3 as the PostgreSQL administrator.

## 12. Application connection defaults

The first-connect test uses:

```text
Host=localhost
Port=5432
Database=DeckKernel
User=deckkernel_user
Integrated=Yes
SslMode=prefer
GssEncMode=disable
KrbSrvName=postgres
ApplicationName=DeckKernel-Scryfall-Test
```

In integrated mode the DeckKernel PostgreSQL adapter does not send the configured
database password.
