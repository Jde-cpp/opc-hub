# Twin of the trigger half of ../mysql/gateway_server_connection_insert.sql - own file: SyncScripts skips
# files named after registered procs, and sqlite triggers can't assign NEW columns, so this is `after update`
# with a corrective update (recursive_triggers is off by default, so it doesn't re-fire itself).
drop trigger if exists gateway_server_connection_update;
GO
create trigger gateway_server_connection_update after update of is_default on gateway_server_connections for each row
when new.is_default and (select count(*) from gateway_server_connections where server_connection_id!=new.server_connection_id and is_default)>0
begin
	update gateway_server_connections set is_default=0 where server_connection_id=new.server_connection_id;
end;
GO
# #520:  the save rule's cross-column half, the mysql trigger's three checks (../mysql/gateway_server_connection_insert.sql).
# raise(abort) is SQLITE_CONSTRAINT_TRIGGER, the app's own error (SqliteException.h).
drop trigger if exists gateway_server_connection_settings;
GO
create trigger gateway_server_connection_settings before update of certificate_uri, security_mode, security_policy on gateway_server_connections for each row
begin
	select raise( abort, 'Security mode Sign or SignAndEncrypt, or a secured policy, needs a certificate URI.' )
		where (new.security_mode in ('Sign', 'SignAndEncrypt') or (new.security_policy is not null and new.security_policy!='http://opcfoundation.org/UA/SecurityPolicy#None')) and new.certificate_uri is null;
	select raise( abort, 'A secured security policy contradicts security mode None.' )
		where new.security_policy is not null and new.security_policy!='http://opcfoundation.org/UA/SecurityPolicy#None' and new.security_mode='None';
	select raise( abort, 'Security policy None contradicts a secured security mode.' )
		where new.security_policy='http://opcfoundation.org/UA/SecurityPolicy#None' and (new.security_mode in ('Sign', 'SignAndEncrypt') or (new.security_mode is null and new.certificate_uri is not null));
end;