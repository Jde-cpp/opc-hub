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
# #520:  the save rule's cross-column half (ServerCnnctn::SettingsError) in the statement that writes the row:  two updates that each
# passed against the saved row cannot together store one it refuses.  Signalled as the app's own error, a 400 carrying the message.
create trigger gateway_server_connection_update before update on gateway_server_connections for each row
begin
	declare no_certificate bool default NEW.certificate_uri is null or NEW.certificate_uri='';
	declare secured_mode bool default ifnull( NEW.security_mode in ('Sign', 'SignAndEncrypt'), false );
	declare secured_policy bool default ifnull( NEW.security_policy!='' and NEW.security_policy!='http://opcfoundation.org/UA/SecurityPolicy#None', false );
	if( (secured_mode or secured_policy) and no_certificate ) then
		signal sqlstate '45000' set message_text = 'Security mode Sign or SignAndEncrypt, or a secured policy, needs a certificate URI.';
	end if;
	if( secured_policy and NEW.security_mode='None' ) then
		signal sqlstate '45000' set message_text = 'A secured security policy contradicts security mode None.';
	end if;
	if( NEW.security_policy='http://opcfoundation.org/UA/SecurityPolicy#None' and (secured_mode or (ifnull(NEW.security_mode, '')='' and not no_certificate)) ) then
		signal sqlstate '45000' set message_text = 'Security policy None contradicts a secured security mode.';
	end if;
	if( NEW.is_default and (select count(*) from gateway_server_connections where server_connection_id!=NEW.server_connection_id and is_default)>0 ) then
		set NEW.is_default = 0;
	end if;
end