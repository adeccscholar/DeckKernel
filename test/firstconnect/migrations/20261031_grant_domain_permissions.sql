-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Grant the DeckKernel application role access to domain catalog tables.
-- Run as the administrator/table owner on database DeckKernel after domain
-- table migrations. Existing rows, sequences and ownership are not changed.
-- Re-entrant: GRANT is idempotent.
--
-- psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261031_grant_domain_permissions.sql

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

DO $grant_domains$
DECLARE
   aTable record;
   strSequence text;
BEGIN
   IF current_database() <> 'DeckKernel' THEN
      RAISE EXCEPTION 'Grant migration must be run on DeckKernel';
   END IF;

   IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'deckkernel_user') THEN
      RAISE EXCEPTION 'Role deckkernel_user does not exist: run test/postgresql_sspi_setup.sql first';
   END IF;

   -- Only catalog tables in the model schema, not arbitrary model/cache tables.
   FOR aTable IN
      SELECT n.nspname, c.relname
      FROM pg_class c
      JOIN pg_namespace n ON n.oid = c.relnamespace
      WHERE n.nspname = 'deckkernel_test'
        AND c.relkind IN ('r','p')
        AND (left(c.relname, 4) = 'dom_' OR c.relname = 'format_types')
      ORDER BY c.relname
   LOOP
      EXECUTE format(
         'GRANT SELECT, INSERT, UPDATE, DELETE ON TABLE %I.%I TO deckkernel_user',
         aTable.nspname, aTable.relname);

      -- Generated identity IDs are backed by sequences. Grant explicit access
      -- without changing their owners or restarting their identity counters.
      strSequence := pg_get_serial_sequence(
         format('%I.%I', aTable.nspname, aTable.relname), 'id');
      IF strSequence IS NOT NULL THEN
         EXECUTE format(
            'GRANT USAGE, SELECT ON SEQUENCE %s TO deckkernel_user',
            strSequence);
      END IF;
   END LOOP;
END
$grant_domains$;

COMMIT;

-- Inspect effective access. A lack of rows means domain tables are not yet installed.
SELECT c.relname AS table_name,
       pg_get_userbyid(c.relowner) AS table_owner,
       has_table_privilege('deckkernel_user',c.oid,'SELECT') AS can_select,
       has_table_privilege('deckkernel_user',c.oid,'INSERT') AS can_insert,
       has_table_privilege('deckkernel_user',c.oid,'UPDATE') AS can_update,
       has_table_privilege('deckkernel_user',c.oid,'DELETE') AS can_delete
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = 'deckkernel_test'
  AND c.relkind IN ('r','p')
  AND (left(c.relname,4) = 'dom_' OR c.relname = 'format_types')
ORDER BY c.relname;
