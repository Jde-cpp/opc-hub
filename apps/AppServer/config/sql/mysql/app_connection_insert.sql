drop procedure if exists app_connection_insert;
GO
#DELIMITER $$;
CREATE PROCEDURE app_connection_insert( _program_name varchar(255), _instance_name varchar(255), _host_name varchar(255), _pid int unsigned, _resource_schema varchar(32) )
begin
	declare _program_id int unsigned;
	declare _instance_id int unsigned;
	declare _connection_id int unsigned;
	select program_id into _program_id from app_programs where name=_program_name;
	if _program_id is null then
		call app_program_insert( _program_name, _program_id );
	end if;
	select instance_id into _instance_id from app_instances where program_id=_program_id and name=_instance_name;
	if _instance_id is null then
		call app_instance_insert( _program_id, _instance_name, _host_name, _instance_id );
	end if;
	update app_instances set resource_schema=_resource_schema where instance_id=_instance_id;

	update app_connections set deleted=now() where instance_id=_instance_id and deleted is null;
	insert into app_connections(instance_id, pid, created) values( _instance_id, _pid, now() );
	select last_insert_id() into _connection_id;
	select _program_id, _instance_id, _connection_id;
end
#$$
#DELIMITER ;