BEGIN TRANSACTION;

DROP TABLE IF EXISTS "db_admin";

CREATE TABLE "db_admin" (
    "entity_id"          INTEGER NOT NULL UNIQUE,
    "entity_name"        TEXT NOT NULL,
    "entity_type"        TEXT,
    "entity_parent"      TEXT,
    "entity_parent_id"   INTEGER,
    "entity_description" TEXT,
    "entity_seq"         INTEGER,
    "entity_values"      TEXT,
    "entity_delims"      TEXT,
    "entity_options"     TEXT,
    "entity_formats"     TEXT,
    "updated_from"       TEXT NOT NULL,
    "updated_ip"         TEXT NOT NULL,
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    UNIQUE("entity_name", "entity_type")
    PRIMARY KEY("entity_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "db_admin_idx1";
DROP INDEX IF EXISTS "db_admin_idx2";

CREATE INDEX "db_admin_idx1" ON "db_admin" ("entity_name");
CREATE INDEX "db_admin_idx2" ON "db_admin" ("entity_type", "entity_parent");

DROP TABLE IF EXISTS "db_event_log";

CREATE TABLE "db_event_log" (
    "log_id"              INTEGER NOT NULL UNIQUE,
    "event_type"          TEXT,
    "application_name"    TEXT,
    "function_name"       TEXT,
    "function_field"      TEXT,
    "db_server"           TEXT NOT NULL,
    "db_database"         TEXT NOT NULL,
    "db_table"            TEXT NOT NULL,
    "db_field"            TEXT,
    "db_serial"           INTEGER,
    "db_value"            TEXT,
    "db_new_value"        TEXT,
    "db_new_value_length" INTEGER,
    "db_sql"              TEXT,
    "db_error"            TEXT,
    "updated_from"        TEXT NOT NULL,
    "updated_ip"          TEXT NOT NULL,
    "updated_by"          TEXT NOT NULL,
    "updated_at"          REAL NOT NULL,
    PRIMARY KEY("log_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "db_event_log_idx1";
DROP INDEX IF EXISTS "db_event_log_idx2";
DROP INDEX IF EXISTS "db_event_log_idx3";

CREATE INDEX "db_event_log_idx1" ON "db_event_log" ("application_name");
CREATE INDEX "db_event_log_idx2" ON "db_event_log" ("db_server");
CREATE INDEX "db_event_log_idx3" ON "db_event_log" ("db_table");

DROP TABLE IF EXISTS "keypress_codes";

CREATE TABLE "keypress_codes" (
    "key_id"       INTEGER NOT NULL UNIQUE,
    "key_name"     TEXT,
    "key_value"    TEXT,
    "key_hex"     TEXT,
    "key_ansi"     TEXT,
    "updated_from" TEXT NOT NULL,
    "updated_ip"   TEXT NOT NULL,
    "updated_by"   TEXT NOT NULL,
    "updated_at"   REAL NOT NULL,
    PRIMARY KEY("key_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "keypress_idx1";

CREATE UNIQUE INDEX "keypress_idx1" ON "keypress_codes" ("key_name");

DROP TABLE IF EXISTS "sessions_commands_inputs";
DROP TABLE IF EXISTS "sessions_commands";
DROP TABLE IF EXISTS "sessions_screens";
DROP TABLE IF EXISTS "sessions";

DROP TABLE IF EXISTS "scripts_commands";
DROP TABLE IF EXISTS "scripts_servers";
DROP TABLE IF EXISTS "scripts";

DROP TABLE IF EXISTS "servers";

DROP TABLE IF EXISTS "servers";

CREATE TABLE "servers" (
    "server_id"          INTEGER NOT NULL UNIQUE,
    "server_name"        TEXT NOT NULL,
    "server_description" TEXT NOT NULL,
    "server_hostname"    TEXT NOT NULL,
    "server_domain"      TEXT,
    "conn_ip"            TEXT NOT NULL,
    "conn_type"          TEXT NOT NULL,
    "conn_port"          INTEGER,
    "UID"                TEXT,
    "PWD"                TEXT,
    "status"             TEXT NOT NULL,
    "status_at"          REAL NOT NULL,
    "updated_from"       TEXT NOT NULL,
    "updated_ip"         TEXT NOT NULL,
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    PRIMARY KEY("server_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "servers_idx1";
DROP INDEX IF EXISTS "servers_idx2";
DROP INDEX IF EXISTS "servers_idx3";

CREATE UNIQUE INDEX "servers_idx1" ON "servers" ("conn_ip", "conn_type", "conn_port");
CREATE UNIQUE INDEX "servers_idx2" ON "servers" ("server_name");
CREATE INDEX "servers_idx3" ON "servers" ("server_hostname");

DROP TABLE IF EXISTS "scripts";

CREATE TABLE "scripts" (
    "script_id"          INTEGER NOT NULL UNIQUE,
    "script_name"        TEXT NOT NULL,
    "script_description" TEXT NOT NULL,
    "created_by"         TEXT NOT NULL,
    "created_at"         REAL NOT NULL,
    "session_inputs"     TEXT,
    "UID"                TEXT,
    "PWD"                TEXT,
    "status"             TEXT NOT NULL,
    "status_at"          REAL NOT NULL,
    "updated_from"       TEXT NOT NULL,
    "updated_ip"         TEXT NOT NULL,
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    PRIMARY KEY("script_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "scripts_idx1";

CREATE INDEX "scripts_idx1" ON "scripts" ("script_name");

DROP TABLE IF EXISTS "scripts_servers";

CREATE TABLE "scripts_servers" (
    "script_svr_id" INTEGER NOT NULL UNIQUE,
    "server_id"     INTEGER NOT NULL,
    "server_name"   TEXT NOT NULL,
    "script_id"     INTEGER NOT NULL,
    "script_name"   TEXT NOT NULL,
    "status"        TEXT NOT NULL,
    "status_at"     REAL NOT NULL,
    "updated_from"  TEXT NOT NULL,
    "updated_ip"    TEXT NOT NULL,
    "updated_by"    TEXT NOT NULL,
    "updated_at"    REAL NOT NULL,
    UNIQUE("server_id", "script_id")
    FOREIGN KEY("server_id") REFERENCES "servers"("server_id"),
    FOREIGN KEY("script_id") REFERENCES "scripts"("script_id"),
    PRIMARY KEY("script_svr_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "scripts_servers_idx1";
DROP INDEX IF EXISTS "scripts_servers_idx2";

CREATE INDEX "scripts_servers_idx1" ON "scripts_servers" ("server_name");
CREATE INDEX "scripts_servers_idx2" ON "scripts_servers" ("script_name");

DROP TABLE IF EXISTS "scripts_commands";

CREATE TABLE "scripts_commands" (
    "script_cmd_id"         INTEGER NOT NULL UNIQUE,
    "script_id"             INTEGER NOT NULL,
    "script_name"           TEXT NOT NULL,
    "command_seq"           INTEGER NOT NULL,
    "command_name"          TEXT,
    "screen_identifier"     TEXT,
    "screen_identifier_pos" TEXT,
    "screen_capture"        TEXT,
    "command_prompt"        TEXT,
    "command_prompt_pos"    TEXT,
    "input_cursor_pos"      TEXT,
    "input_command"         TEXT,
    "input_hidden"          TEXT,
    "submit_key"            TEXT,
    "pause_before_input"    REAL,
    "updated_from"          TEXT NOT NULL,
    "updated_ip"            TEXT NOT NULL,
    "updated_by"            TEXT NOT NULL,
    "updated_at"            REAL NOT NULL,
    UNIQUE("script_id", "command_seq")
    FOREIGN KEY("script_id") REFERENCES "scripts"("script_id"),
    PRIMARY KEY("script_cmd_id" AUTOINCREMENT)
);

CREATE UNIQUE INDEX "scripts_commands_idx1" ON "scripts_commands" ("script_id", "script_name", "command_seq");

DROP TABLE IF EXISTS "sessions";

CREATE TABLE "sessions" (
    "session_id"        INTEGER NOT NULL UNIQUE,
    "session_name"      TEXT NOT NULL,
    "server_id"         INTEGER NOT NULL,
    "server_name"       TEXT NOT NULL,
    "script_id"         INTEGER NOT NULL,
    "script_name"       TEXT NOT NULL,
    "script_commands"   INTEGER NOT NULL,
    "session_start_at"  REAL NOT NULL,
    "session_finish_at" REAL,
    "session_status"    TEXT NOT NULL,
    "updated_from"      TEXT NOT NULL,
    "updated_ip"        TEXT NOT NULL,
    "updated_by"        TEXT NOT NULL,
    "updated_at"        REAL NOT NULL,
    FOREIGN KEY("server_id", "script_id") REFERENCES "scripts_servers"("server_id", "script_id"),
    PRIMARY KEY("session_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "sessions_idx1";

CREATE UNIQUE INDEX "sessions_idx1" ON "sessions" ("session_name");

DROP TABLE IF EXISTS "sessions_commands";

CREATE TABLE "sessions_commands" (
    "session_cmd_id"             INTEGER NOT NULL UNIQUE,
    "session_id"                 INTEGER NOT NULL,
    "script_id"                  INTEGER NOT NULL,
    "script_cmd_id"              INTEGER NOT NULL,
    "command_seq"                INTEGER NOT NULL,
    "screen_identifier"          TEXT,
    "screen_identifier_pos"      TEXT,
    "screen_capture"             TEXT,
    "command_prompt"             TEXT,
    "command_prompt_pos"         TEXT,
    "input_cursor_pos"           TEXT,
    "input_command"              TEXT,
    "input_hidden"               TEXT,
    "submit_key"                 TEXT,
    "pause_before_input"         REAL,
    "screen_scrn_identifier_pos" TEXT,
    "screen_command_prompt_pos"  TEXT,
    "screen_input_cursor_pos"    TEXT,
    "command_prompt_ok"          TEXT,
    "input_processed"            TEXT,
    "submit_key_processed"       TEXT,
    "current_cursor_pos"         TEXT,
    "input_processed_ascii"      TEXT,
    "session_scrn_id"            INTEGER,
    "updated_from"               TEXT NOT NULL,
    "updated_ip"                 TEXT NOT NULL,
    "updated_by"                 TEXT NOT NULL,
    "updated_at"                 REAL NOT NULL,
    FOREIGN KEY("session_scrn_id") REFERENCES "sessions_screens"("session_scrn_id"),
    PRIMARY KEY("session_cmd_id" AUTOINCREMENT)
);

DROP TABLE IF EXISTS "sessions_commands_inputs";

CREATE TABLE "sessions_commands_inputs" (
    "session_inp_id" INTEGER NOT NULL UNIQUE,
    "session_id"     INTEGER NOT NULL,
    "script_id"      INTEGER NOT NULL,
    "script_cmd_id"  INTEGER NOT NULL,
    "command_seq"    INTEGER NOT NULL,
    "command_name"   TEXT,
    "command_value"  TEXT,
    "command_status" TEXT NOT NULL,
    "updated_from"   TEXT NOT NULL,
    "updated_ip"     TEXT NOT NULL,
    "updated_by"     TEXT NOT NULL,
    "updated_at"     REAL NOT NULL,
    UNIQUE("script_cmd_id", "session_id")
    FOREIGN KEY("session_id") REFERENCES "sessions"("session_id"),
    FOREIGN KEY("script_cmd_id") REFERENCES "scripts_commands"("script_cmd_id"),
    PRIMARY KEY("session_inp_id" AUTOINCREMENT)
);

DROP TABLE IF EXISTS "sessions_screens";

CREATE TABLE "sessions_screens" (
    "session_scrn_id"          INTEGER NOT NULL UNIQUE,
    "session_id"               INTEGER NOT NULL,
    "session_cmd_id_from"      INTEGER,
    "session_cmd_id_to"        INTEGER,
    "session_command_seq_from" INTEGER NOT NULL,
    "session_command_seq_to"   INTEGER NOT NULL,
    "terminal_output_ascii"    TEXT,
    "terminal_output_raw"      TEXT,
    "terminal_screen"          TEXT,
    "updated_from"             TEXT NOT NULL,
    "updated_ip"               TEXT NOT NULL,
    "updated_by"               TEXT NOT NULL,
    "updated_at"               REAL NOT NULL,
    FOREIGN KEY("session_id") REFERENCES "sessions"("session_id"),
    PRIMARY KEY("session_scrn_id" AUTOINCREMENT)
);

DROP VIEW IF EXISTS sessions_commands_inputs_pending;

CREATE VIEW sessions_commands_inputs_pending 
AS
SELECT sessions_commands_inputs.session_inp_id
     , sessions_commands_inputs.session_id
     , sessions.session_name
     , COALESCE((SELECT MAX(sessions_commands.command_seq)
                 FROM   sessions_commands
                 WHERE  sessions_commands.session_id = sessions.session_id) || ' of ' ||
                (SELECT MAX(scripts_commands.command_seq)
                 FROM   scripts_commands
                 WHERE  scripts_commands.script_id = sessions_commands_inputs.script_id), 'None') As session_commands_run
     , sessions.session_status
     , sessions.updated_at As session_status_at
     , sessions_commands_inputs.script_id
     , scripts.script_name
     , sessions_commands_inputs.script_cmd_id
     , sessions_commands_inputs.command_seq
     , scripts_commands.screen_identifier
     , scripts_commands.screen_identifier_pos
     , scripts_commands.command_prompt
     , scripts_commands.command_prompt_pos
     , sessions_commands_inputs.command_name
     , sessions_commands_inputs.command_value
     , sessions_commands_inputs.command_status
     , sessions_commands_inputs.updated_from
     , sessions_commands_inputs.updated_ip
     , sessions_commands_inputs.updated_by
     , sessions_commands_inputs.updated_at
FROM   sessions_commands_inputs
       LEFT JOIN sessions ON sessions.session_id = sessions_commands_inputs.session_id
       LEFT JOIN scripts ON scripts.script_id = sessions_commands_inputs.script_id
       LEFT JOIN scripts_commands ON scripts_commands.script_id = sessions_commands_inputs.script_id
                                 AND scripts_commands.command_seq = sessions_commands_inputs.command_seq
WHERE EXISTS (SELECT sessions_commands_inputs2.session_id
              FROM   sessions_commands_inputs as sessions_commands_inputs2
              WHERE  sessions_commands_inputs2.session_id = sessions.session_id
              AND NOT sessions_commands_inputs.command_status = 'updated');

DROP TRIGGER IF EXISTS sessions_commands_inputs_created;

CREATE TRIGGER sessions_commands_inputs_created
AFTER INSERT ON sessions
BEGIN  

    INSERT INTO sessions_commands_inputs(session_id, script_id, script_cmd_id, command_seq, command_name, command_value, command_status, updated_from, updated_ip, updated_by, updated_at)
    SELECT new.session_id
         , scripts_commands.script_id
         , scripts_commands.script_cmd_id
         , scripts_commands.command_seq
         , scripts_commands.command_name
         , null
         , 'created'
         , new.updated_from
         , new.updated_ip
         , new.updated_by
         , new.updated_at
    FROM   scripts_commands
    WHERE  scripts_commands.script_id = new.script_id
    AND    scripts_commands.input_command = '##user_input##'
    ORDER BY scripts_commands.command_seq;

    UPDATE sessions
    SET    script_commands = (SELECT COUNT(*)
                              FROM   scripts_commands
							  WHERE  script_id = new.script_id)
    WHERE  sessions.session_id = new.session_id;

    UPDATE sessions
    SET    session_status = 'waiting'
    WHERE  sessions.session_id = new.session_id
    AND NOT EXISTS (SELECT sessions_commands_inputs.session_inp_id
                    FROM   sessions_commands_inputs
                    WHERE  sessions_commands_inputs.session_id = new.session_id);

END;

DROP TRIGGER IF EXISTS sessions_commands_inputs_updated;

CREATE TRIGGER sessions_commands_inputs_updated
AFTER UPDATE ON sessions_commands_inputs
BEGIN

    UPDATE sessions_commands_inputs
    SET    command_status = 'updated'
    WHERE  sessions_commands_inputs.session_inp_id = new.session_inp_id
    AND    new.command_value IS NOT NULL;
    
    UPDATE sessions
    SET    session_status = 'waiting'
         , updated_from = new.updated_from
         , updated_ip = new.updated_ip
         , updated_by = new.updated_by
         , updated_at = new.updated_at
    WHERE  sessions.session_id = new.session_id
    AND    session_status = 'created'
    AND NOT EXISTS (SELECT sessions_commands_inputs.session_inp_id
                    FROM   sessions_commands_inputs
                    WHERE  sessions_commands_inputs.session_id = new.session_id
                    AND    sessions_commands_inputs.command_status = 'created');
END;

COMMIT;
