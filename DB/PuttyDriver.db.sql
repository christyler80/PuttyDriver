BEGIN TRANSACTION;

DROP TABLE IF EXISTS "keypress_codes";

CREATE TABLE IF NOT EXISTS "keypress_codes" (
    "key_id"       INTEGER NOT NULL UNIQUE,
    "key_name"     TEXT,
    "key_ansi"     TEXT,
    "updated_from" TEXT NOT NULL,
    "updated_by"   TEXT NOT NULL,
    "updated_at"   REAL NOT NULL,
    PRIMARY KEY("key_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "keypress_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "keypress_idx1" ON "keypress_codes" (
    "key_name"
);

DROP INDEX IF EXISTS "keypress_idx2";

CREATE UNIQUE INDEX IF NOT EXISTS "keypress_idx2" ON "keypress_codes" (
    "key_ansi"
);

DROP TABLE IF EXISTS "servers";

CREATE TABLE IF NOT EXISTS "servers" (
    "server_id"    INTEGER NOT NULL UNIQUE,
    "server_name"  TEXT NOT NULL,
    "conn_ip"      TEXT NOT NULL,
    "conn_type"    TEXT NOT NULL,
    "conn_port"    INTEGER,
    "updated_from" TEXT NOT NULL,
    "updated_by"   TEXT NOT NULL,
    "updated_at"   REAL NOT NULL,
    PRIMARY KEY("server_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "servers_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "servers_idx1" ON "servers" (
    "server_name"
);

DROP TABLE IF EXISTS "scripts";

CREATE TABLE IF NOT EXISTS "scripts" (
    "script_id"    INTEGER NOT NULL UNIQUE,
    "script_name"  TEXT NOT NULL,
    "updated_from" TEXT NOT NULL,
    "updated_by"   TEXT NOT NULL,
    "updated_at"   REAL NOT NULL,
    PRIMARY KEY("script_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "scripts_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "scripts_idx1" ON "scripts" (
    "script_name"
);

DROP TABLE IF EXISTS "scripts_commands";

CREATE TABLE IF NOT EXISTS "scripts_commands" (
    "script_cmd_id"      INTEGER NOT NULL UNIQUE,
    "script_id"          INTEGER NOT NULL,
    "command_seq"        INTEGER NOT NULL,
    "screen_identifier"  TEXT,
    "screen_capture"     TEXT,
    "command_prompt"     TEXT,
    "input_cursor_pos"   TEXT,
    "input_command"      TEXT,
    "input_hidden"       TEXT,
    "submit_key"         TEXT,
    "pause_before_input" REAL,
    "updated_from"       TEXT NOT NULL,
    "updated_by"         TEXT NOT NULL,
    "updated_at"         REAL NOT NULL,
    FOREIGN KEY("script_id") REFERENCES "scripts"("script_id"),
    PRIMARY KEY("script_cmd_id" AUTOINCREMENT)
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
    FOREIGN KEY("server_id") REFERENCES "servers"("server_id"),
    FOREIGN KEY("script_id") REFERENCES "scripts"("script_id"),
    PRIMARY KEY("session_id" AUTOINCREMENT)
);

DROP INDEX IF EXISTS "sessions_idx1";

CREATE UNIQUE INDEX IF NOT EXISTS "sessions_idx1" ON "sessions" (
    "session_name"
);

DROP TABLE IF EXISTS "sessions_commands";

CREATE TABLE IF NOT EXISTS "sessions_commands" (
    "session_cmd_id"          INTEGER NOT NULL UNIQUE,
    "session_id"              INTEGER NOT NULL,
    "command_seq"             INTEGER NOT NULL,
    "screen_identifier"       TEXT,
    "screen_capture"          TEXT,
    "input_prompt"            TEXT,
    "input_cursor_pos"        TEXT,
    "input_command"           TEXT,
    "input_hidden"            TEXT,
    "submit_key"              TEXT,
    "pause_before_input"      REAL,
    "prompt_cursor_position"  TEXT,
    "command_prompt_ok"       TEXT,
    "input_processed"         TEXT,
    "submit_key_processed"    TEXT,
    "current_cursor_position" TEXT,
    "input_processed_ascii"   TEXT,
    "updated_from"            TEXT NOT NULL,
    "updated_by"              TEXT NOT NULL,
    "updated_at"              REAL NOT NULL,
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

COMMIT;
