create or alter proc [dbo].app_connection_insert( @program_name varchar(255), @instance_name varchar(255), @host_name varchar(255), @pid int out, @resource_schema varchar(32) ) as
	set nocount on;
	declare @program_id int;
	declare @instance_id int;
	declare @connection_id int;
	select @program_id=program_id from [dbo].app_programs where name=@program_name;
	if( @program_id is null )
		exec [dbo].app_program_insert @program_name, @program_id out;
	select @instance_id=instance_id from [dbo].app_instances where program_id=@program_id and name=@instance_name;
	if( @instance_id is null )
		exec [dbo].app_instance_insert @program_id, @instance_name, @host_name, @instance_id out;
	update [dbo].app_instances set resource_schema=@resource_schema where instance_id=@instance_id;

	update [dbo].app_connections set deleted=getutcdate() where instance_id=@instance_id and deleted is null;
	insert into [dbo].app_connections(instance_id, pid, created) values( @instance_id, @pid, getutcdate() );
	set @connection_id=@@identity;
	select @program_id, @instance_id, @connection_id;
go
