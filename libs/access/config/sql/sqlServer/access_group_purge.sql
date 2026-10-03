create or alter proc [dbo].access_group_purge( @identity_id int ) as begin
	if exists( select 1 from access_identities where identity_id=@identity_id and is_group=1 ) begin
		delete from access_acl where identity_id=@identity_id;
		delete from access_groups where identity_id=@identity_id or member_id=@identity_id;
	end
	delete from access_identities where identity_id=@identity_id and is_group=1;
end
