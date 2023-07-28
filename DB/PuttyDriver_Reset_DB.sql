BEGIN TRANSACTION;

/* Important : run 'PuttyDriver_Clear_Sessions_Tables.sql' before running this script. */

DROP TABLE IF EXISTS "db_event_log";

CREATE TABLE IF NOT EXISTS "db_event_log" (
    "log_id"              INTEGER NOT NULL UNIQUE,
    "event_type"          TEXT,
    "application_name"    TEXT,
    "function_name"       TEXT,
    "function_field"      TEXT,
    "db_server"           TEXT NOT NULL,
    "db_database"         TEXT NOT NULL,
    "db_table"            TEXT NOT NULL,
    "db_serial"           INTEGER,
    "db_field"            TEXT,
    "db_value"            TEXT,
    "db_new_value"        TEXT,
    "db_new_value_length" INTEGER,
    "db_new_value_sql"    TEXT,
    "db_error"            TEXT,
    "updated_from"        TEXT NOT NULL,
    "updated_by"          TEXT NOT NULL,
    "updated_at"          REAL NOT NULL,
    PRIMARY KEY("log_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "db_event_log_idx1";

CREATE INDEX IF NOT EXISTS "db_event_log_idx1" ON "db_event_log" ("application_name");
CREATE INDEX IF NOT EXISTS "db_event_log_idx2" ON "db_event_log" ("db_server");
CREATE INDEX IF NOT EXISTS "db_event_log_idx3" ON "db_event_log" ("db_table");


DROP TABLE IF EXISTS "servers";

CREATE TABLE IF NOT EXISTS "servers" (
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
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    PRIMARY KEY("server_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "servers_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "servers_idx1" ON "servers" (
    "conn_ip", "conn_type", "conn_port"
);

DROP INDEX IF EXISTS "servers_idx2";

CREATE UNIQUE INDEX IF NOT EXISTS "servers_idx2" ON "servers" (
    "server_name"
);

DROP INDEX IF EXISTS "servers_idx3";

CREATE INDEX IF NOT EXISTS "servers_idx3" ON "servers" (
    "server_hostname"
);

DROP TABLE IF EXISTS "scripts";

CREATE TABLE IF NOT EXISTS "scripts" (
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
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    PRIMARY KEY("script_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "scripts_idx1";

CREATE INDEX IF NOT EXISTS "scripts_idx1" ON "scripts" (
    "script_name"
);

DROP TABLE IF EXISTS "scripts_servers";

CREATE TABLE IF NOT EXISTS "scripts_servers" (
    "script_svr_id" INTEGER NOT NULL UNIQUE,
    "server_id"     INTEGER NOT NULL,
    "server_name"   TEXT NOT NULL,
    "script_id"     INTEGER NOT NULL,
    "script_name"   TEXT NOT NULL,
    "status"        TEXT NOT NULL,
    "status_at"    REAL NOT NULL,
    "updated_from"  TEXT NOT NULL,
    "updated_by"    TEXT NOT NULL,
    "updated_at"    REAL NOT NULL,
    UNIQUE("server_id", "script_name")
    FOREIGN KEY("server_id", "server_name") REFERENCES "servers"("server_id", "server_name"),
    FOREIGN KEY("script_id", "script_name") REFERENCES "scripts"("script_id", "script_name"),
    PRIMARY KEY("script_svr_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "scripts_servers_idx1";

CREATE INDEX IF NOT EXISTS "scripts_servers_idx1" ON "scripts_servers" (
    "server_name"
);

DROP INDEX IF EXISTS "scripts_servers_idx2";

CREATE INDEX IF NOT EXISTS "scripts_servers_idx2" ON "scripts_servers" (
    "script_name"
);

DROP TABLE IF EXISTS "scripts_commands";

CREATE TABLE IF NOT EXISTS "scripts_commands" (
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
    "updated_by"            TEXT NOT NULL,
    "updated_at"            REAL NOT NULL,
    UNIQUE("script_id", "command_seq")
    FOREIGN KEY("script_id", "script_name") REFERENCES "scripts"("script_id", "script_name"),
    PRIMARY KEY("script_cmd_id" AUTOINCREMENT)
);

CREATE UNIQUE INDEX IF NOT EXISTS "scripts_commands_idx1" ON "scripts_commands" (
    "script_id", "script_name", "command_seq"
);

DROP TABLE IF EXISTS "sessions";

CREATE TABLE IF NOT EXISTS "sessions" (
    "session_id"        INTEGER NOT NULL UNIQUE,
    "session_name"      TEXT NOT NULL,
    "server_id"         INTEGER NOT NULL,
    "server_name"       TEXT NOT NULL,
    "script_id"         INTEGER NOT NULL,
    "script_name"       TEXT NOT NULL,
    "session_start_at"  REAL NOT NULL,
    "session_finish_at" REAL,
    "session_status"    TEXT NOT NULL,
    "updated_from"      TEXT NOT NULL,
    "updated_by"        TEXT NOT NULL,
    "updated_at"        REAL NOT NULL,
    FOREIGN KEY("server_id", "server_name") REFERENCES "servers"("server_id", "server_name"),
    FOREIGN KEY("script_id", "script_name") REFERENCES "scripts"("script_id", "script_name"),
    PRIMARY KEY("session_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "sessions_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "sessions_idx1" ON "sessions" (
    "session_name"
);

DROP TABLE IF EXISTS "sessions_commands";

CREATE TABLE IF NOT EXISTS "sessions_commands" (
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
    "updated_from"               TEXT NOT NULL,
    "updated_by"                 TEXT NOT NULL,
    "updated_at"                 REAL NOT NULL,
    FOREIGN KEY("session_id") REFERENCES "sessions"("session_id"),
    PRIMARY KEY("session_cmd_id" AUTOINCREMENT)
);

DROP TABLE IF EXISTS "sessions_screens";

CREATE TABLE IF NOT EXISTS "sessions_screens" (
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
    "updated_by"               TEXT NOT NULL,
    "updated_at"               REAL NOT NULL,
    FOREIGN KEY("session_cmd_id_from") REFERENCES "sessions_commands"("session_cmd_id"),
    FOREIGN KEY("session_id") REFERENCES "sessions"("session_id"),
    FOREIGN KEY("session_cmd_id_to") REFERENCES "sessions_commands"("session_cmd_id"),
    PRIMARY KEY("session_scrn_id" AUTOINCREMENT)
);

DROP TABLE IF EXISTS "sessions_commands_inputs";

CREATE TABLE IF NOT EXISTS "sessions_commands_inputs" (
    "session_inp_id" INTEGER NOT NULL UNIQUE,
    "session_id"     INTEGER NOT NULL,
    "script_id"      INTEGER NOT NULL,
    "script_cmd_id"  INTEGER NOT NULL,
    "command_seq"    INTEGER NOT NULL,
    "command_name"   TEXT,
    "command_value"  TEXT,
    "updated_from"   TEXT NOT NULL,
    "updated_by"     TEXT NOT NULL,
    "updated_at"     REAL NOT NULL,
    UNIQUE("script_cmd_id", "session_id")
    FOREIGN KEY("script_cmd_id") REFERENCES "scripts_commands"("script_cmd_id"),
    PRIMARY KEY("session_inp_id" AUTOINCREMENT)
);

COMMIT;
