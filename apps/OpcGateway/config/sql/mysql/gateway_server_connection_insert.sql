drop procedure if exists gateway_server_connection_insert;
GO

create procedure gateway_server_connection_insert( _name varchar(255), _slug varchar(255), _attributes smallint unsigned, _description varchar(2047), _is_default bit, _default_browse_ns smallint unsigned, _certificate_uri varchar(2047), _url varchar(2047), _security_mode varchar(16), _security_policy varchar(256), _session_timeout int unsigned, _request_timeout int unsigned, _ping_interval int unsigned, _ttl int unsigned, _locale varchar(256), out _server_connection_id int unsigned ) begin
	if( _is_default ) then
		update gateway_server_connections set is_default=0;
	end if;
	insert into gateway_server_connections( name,attributes,created,slug,description,certificate_uri, is_default, default_browse_ns, url, security_mode, security_policy, session_timeout, request_timeout, ping_interval, ttl, locale )
		values( _name, _attributes, CURRENT_TIMESTAMP(), _slug, _description, _certificate_uri, _is_default, _default_browse_ns, _url, _security_mode, _security_policy, _session_timeout, _request_timeout, _ping_interval, _ttl, _locale );
	set _server_connection_id = LAST_INSERT_ID();
end
GO
drop trigger if exists gateway_server_connection_update;
GO
create trigger gateway_server_connection_update before update on gateway_server_connections for each row
  if( NEW.is_default and (select count(*) from gateway_server_connections where server_connection_id!=NEW.server_connection_id and is_default)>0 ) then
    set NEW.is_default = 0;
  end if;