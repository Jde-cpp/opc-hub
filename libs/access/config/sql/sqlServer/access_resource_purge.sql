-- The grants on the resource go with it (#357:  a hist group's own row, purged with the group by its gateway).  Each
-- access_permission_rights row on it names a permission access_role_add or access_ac_upsert_permission minted for that one
-- resource, so the permission goes whole - its acl rows, its role memberships and the permission itself - once its rights
-- row is gone.  A role is a permission too, but never holds rights of its own on a resource:  is_role=0 keeps one out of
-- the list should a row ever say otherwise;  its rights row still goes.  The members are collected first, as
-- access_role_purge's are:  the rights rows must go before the permission rows can (fk), and would take the list with them.
create or alter proc [dbo].access_resource_purge( @resource_id int )as begin
	declare @members table( permission_id int primary key );
	insert into @members
	select r.permission_id
	from access_permission_rights r
		join access_permissions p on p.permission_id=r.permission_id
	where r.resource_id=@resource_id
		and p.is_role=0;

	delete from access_acl where permission_id in ( select permission_id from @members );
	delete from access_role_members where member_id in ( select permission_id from @members );
	delete from access_permission_rights where resource_id=@resource_id;
	delete from access_permissions where permission_id in ( select permission_id from @members );
	delete from access_resources where resource_id=@resource_id;
end