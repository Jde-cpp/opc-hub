drop procedure if exists access_user_purge;
go

create procedure access_user_purge( _identity_id int unsigned )
begin
	if exists( select 1 from access_identities where identity_id=_identity_id and is_group=0 ) then
		delete from access_profiles where identity_id=_identity_id;
		delete from access_acl where identity_id=_identity_id;
		delete from `access_groups` where identity_id=_identity_id or member_id=_identity_id;
		delete from access_users where identity_id=_identity_id;
	end if;
	delete from access_identities where identity_id=_identity_id and is_group=0;
end
