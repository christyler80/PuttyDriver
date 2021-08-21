DELETE FROM sessions_screens WHERE 1 = 1; UPDATE sqlite_sequence set seq = 0 WHERE name = "sessions_screens";
DELETE FROM sessions_commands WHERE 1 = 1; UPDATE sqlite_sequence set seq = 0 WHERE name = "sessions_commands";
DELETE FROM sessions WHERE 1 = 1; UPDATE sqlite_sequence set seq = 0 WHERE name = "sessions";
