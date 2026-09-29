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

## 3. Create the application role and test schema

While connected to `DeckKernel` as an administrator, execute:

[postgresql_sspi_setup.sql](../../test/postgresql_sspi_setup.sql)

The script:

- creates `deckkernel_user LOGIN` if necessary;
- grants it connection access to `DeckKernel`;
- creates and assigns ownership of schema `deckkernel_test`;
- grants schema usage/create rights;
- repairs ownership of existing first-connect test tables when they were
  previously created by an administrator.

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

## 5. Determine the Windows identity

In the same Windows account that will run DeckKernel:

```bat
whoami
```

Typical domain output:

```text
DOMAIN\username
```

The working SSPI connection can later be verified with `system_user`.

## 6. Configure pg_ident.conf

Add a dedicated mapping from the Windows identity to the PostgreSQL role:

```text
# MAPNAME           SYSTEM-USERNAME       PG-USERNAME
deckkernel_sspi     DOMAIN\username       deckkernel_user
```

Replace `DOMAIN\username` with the actual Windows account.

## 7. Configure pg_hba.conf

Add the DeckKernel-specific SSPI rules before broader localhost rules such as
`scram-sha-256`:

```text
# TYPE  DATABASE    USER               ADDRESS          METHOD  OPTIONS
host    DeckKernel  deckkernel_user    127.0.0.1/32     sspi    include_realm=1 map=deckkernel_sspi
host    DeckKernel  deckkernel_user    ::1/128          sspi    include_realm=1 map=deckkernel_sspi
```

A typical resulting order is:

```text
host    DeckKernel  deckkernel_user    127.0.0.1/32     sspi    include_realm=1 map=deckkernel_sspi
host    DeckKernel  deckkernel_user    ::1/128          sspi    include_realm=1 map=deckkernel_sspi
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
   map_name,
   sys_name,
   pg_username,
   error
FROM pg_ident_file_mappings
ORDER BY map_number;
```

The DeckKernel HBA rules and identity mapping should have `error IS NULL`.

## 9. Test SSPI independently with psql

Use the PostgreSQL command-line client before testing DeckKernel:

```bat
"C:\Program Files\PostgreSQL\18\bin\psql.exe" -h localhost -p 5432 -d DeckKernel -U deckkernel_user
```

No PostgreSQL password should be requested.

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

## 10. Verify ownership of existing test objects

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

If not, rerun:

```text
test\postgresql_sspi_setup.sql
```

as the PostgreSQL administrator.

## 11. Application connection defaults

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
