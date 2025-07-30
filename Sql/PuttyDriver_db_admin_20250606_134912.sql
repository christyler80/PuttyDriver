UPDATE [db_admin] SET [entity_values] = 'Driver=SQLite3 ODBC Driver;Database=##AppPath##\DB\PuttyDriver.db;Version=3;' WHERE [entity_id] = 1003
UPDATE [db_admin] SET [entity_description] = 'Sessions created and waiting for command inputs to be added via table ''sessions_commands_inputs''.' WHERE [entity_id] = 1035
UPDATE [db_admin] SET [entity_description] = 'Sessions with command inputs loaded via tables ''sessions_commands_inputs'', ready to run but not yet scheduled.' WHERE [entity_id] = 1036
UPDATE [db_admin] SET [entity_description] = 'Sessions with command inputs loaded via tables ''sessions_commands_inputs'' and scheduled to run at date/time specified in table.field ''sessions.session_start_at'' table field.' WHERE [entity_id] = 1037
