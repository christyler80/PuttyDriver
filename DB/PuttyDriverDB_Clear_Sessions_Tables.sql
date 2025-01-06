BEGIN TRANSACTION;

DELETE FROM sessions_screens WHERE 1 = 1; UPDATE sqlite_sequence set seq = 0 WHERE name = "sessions_screens";
DELETE FROM sessions_commands WHERE 1 = 1; UPDATE sqlite_sequence set seq = 0 WHERE name = "sessions_commands";

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

COMMIT;
