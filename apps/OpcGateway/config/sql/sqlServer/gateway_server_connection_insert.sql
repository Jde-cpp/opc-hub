create or alter proc [dbo].gateway_server_connection_insert @name varchar(255), @slug varchar(255), @attributes smallint, @description varchar(2047), @is_default bit, @default_browse_ns smallint, @certificate_uri varchar(2047), @url varchar(2047), @security_mode varchar(16), @security_policy varchar(256), @session_timeout int, @request_timeout int, @ping_interval int, @ttl int, @locale varchar(256), @server_connection_id int output as begin
	set nocount on;
  if @is_default=1
    update [dbo].gateway_server_connections set is_default=0;
  insert into [dbo].gateway_server_connections( name, attributes, created, slug, description, certificate_uri, is_default, default_browse_ns, url, security_mode, security_policy, session_timeout, request_timeout, ping_interval, ttl, locale )
    values( @name, @attributes, getutcdate(), @slug, @description, @certificate_uri, @is_default, @default_browse_ns, @url, @security_mode, @security_policy, @session_timeout, @request_timeout, @ping_interval, @ttl, @locale );
  set @server_connection_id = SCOPE_IDENTITY();
end
go
create or alter trigger [dbo].gateway_server_connection_update on [dbo].gateway_server_connections for update as begin
	declare @is_default bit, @id int;
	select @is_default=is_default, @id=server_connection_id from inserted;
	if @is_default=1
		update [dbo].gateway_server_connections set is_default=0 where server_connection_id!=@id;
end
go