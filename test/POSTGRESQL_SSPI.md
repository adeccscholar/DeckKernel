# PostgreSQL integrated Windows authentication for DeckKernel

The functional test uses PostgreSQL through libpq/libpqxx and is configured for
Windows integrated authentication by default.

The application defaults are:

```text
host            localhost
port            5432
database        DeckKernel
database role   deckkernel_user
integrated      true
password        not sent
sslmode         prefer
gssencmode      disable
gsslib          not specified
krbsrvname      postgres
```

On Windows, leaving `gsslib` unspecified lets libpq use its normal Windows
SSPI path. `gssencmode=disable` only disables GSSAPI transport encryption; it
does not disable SSPI authentication.

## 1. Determine the active PostgreSQL configuration files

Connect once as the PostgreSQL administrator, for example with pgAdmin, and run:

```sql
SHOW hba_file;
SHOW ident_file;
SHOW data_directory;
```

Do not guess the installation directory. PostgreSQL can place these files outside
the default data directory.

## 2. Create the DeckKernel database role

Connect pgAdmin to database `DeckKernel` as an administrator and execute:

```text
test/postgresql_sspi_setup.sql
```

This creates:

```text
deckkernel_user
```

as a PostgreSQL login role without a password and makes it owner of the
`deckkernel_test` schema.

The role name deliberately does not have to be identical to the Windows account.
The mapping between Windows identity and PostgreSQL role is handled by
`pg_ident.conf`.

## 3. Determine the Windows identity

In the same Windows account that will run the test program:

```bat
whoami
```

Typical output is similar to:

```text
DOMAIN\username
```

For a local Windows account it is normally similar to:

```text
COMPUTERNAME\username
```

Use the actual value from your machine in the next step.

## 4. Configure pg_ident.conf

Add one explicit mapping:

```text
# MAPNAME           SYSTEM-USERNAME          PG-USERNAME
deckkernel_sspi     DOMAIN\username         deckkernel_user
```

Replace `DOMAIN\username` with the identity returned by `whoami`.

Keeping a dedicated PostgreSQL role is preferable to creating PostgreSQL roles
named after every Windows account. It also keeps database permissions independent
of the Windows account naming scheme.

## 5. Configure pg_hba.conf

Add these rules before broader localhost rules such as
`scram-sha-256` or `md5`:

```text
# TYPE  DATABASE    USER               ADDRESS          METHOD  OPTIONS
host    DeckKernel  deckkernel_user    127.0.0.1/32     sspi    map=deckkernel_sspi
host    DeckKernel  deckkernel_user    ::1/128          sspi    map=deckkernel_sspi
```

The order is important. PostgreSQL uses the first rule whose connection type,
database, user, and address match. It does not continue with later rules after
an authentication failure.

The two entries restrict SSPI to:

- database `DeckKernel`;
- PostgreSQL role `deckkernel_user`;
- local IPv4 and IPv6 loopback connections.

Other databases and roles continue to use the existing authentication rules.

## 6. Reload and validate the server configuration

Run as PostgreSQL administrator:

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

Both DeckKernel entries should have `error IS NULL`.

On Windows, new connections see pg_hba.conf changes immediately, but reloading is
still useful because pg_ident.conf is a separately loaded configuration file.

## 7. Test SSPI independently before running DeckKernel

If the PostgreSQL command-line client is available, test the same connection
without a password:

```bat
psql -h localhost -p 5432 -d DeckKernel -U deckkernel_user
```

It should connect without prompting for a password.

Then verify the effective database identity:

```sql
SELECT current_database(), current_user, session_user;
```

Expected database and role:

```text
DeckKernel
deckkernel_user
deckkernel_user
```

## 8. Run the DeckKernel functional test

No environment variables are required for the standard local setup:

```bat
test\build\deckkernel_scryfall_postgres_test.exe
```

The program now defaults to integrated authentication.

Environment variables remain available for overriding the defaults:

```text
DECKKERNEL_PGHOST
DECKKERNEL_PGPORT
DECKKERNEL_PGDATABASE
DECKKERNEL_PGUSER
DECKKERNEL_PGPASSWORD
DECKKERNEL_PG_INTEGRATED
DECKKERNEL_PGSSLMODE
DECKKERNEL_PG_GSSENCMODE
DECKKERNEL_PG_GSSLIB
DECKKERNEL_PG_KRBSRVNAME
```

For example, to temporarily fall back to password authentication:

```bat
set DECKKERNEL_PG_INTEGRATED=false
set DECKKERNEL_PGUSER=postgres
set DECKKERNEL_PGPASSWORD=...
```

## 9. Kerberos versus NTLM

PostgreSQL SSPI on Windows uses the Windows Negotiate security provider. It uses
Kerberos when that is available and can fall back to NTLM otherwise.

For the first local functional test, no SPN work should be assumed necessary.
When DeckKernel later connects to a PostgreSQL server on another machine and
Kerberos is required explicitly, the PostgreSQL service identity and SPN must be
configured as a separate deployment concern.
