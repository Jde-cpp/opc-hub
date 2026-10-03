drop procedure if exists access_role_purge;
go

# The role's own permissions - the is_role=false members access_role_add minted for it - go with it.  A member that is
# itself a role is shared and keeps its acl grants, rights and other memberships:  only this role's membership row goes
# (the old `permission_id in ( members )` deletes stripped every direct grant of every child role).  The same courtesy
# goes to a bare member something else still references - another role's membership, an acl row.  The owned members are
# collected first:  the membership rows must go before the permission rows can (fk), and would take the list with them.
# The role's own access_permissions row goes last, after the temporary table is dropped:  a call reports the row count of
# its last statement, and a purge that reports 0 is not published - the access caches kept the purged role's grants.
create procedure access_role_purge( _role_id int unsigned )
begin
	create temporary table if not exists access_role_purge_members( permission_id int unsigned primary key );
	delete from access_role_purge_members;
	insert into access_role_purge_members
	select m.member_id
	from access_role_members m
		join access_permissions p on p.permission_id=m.member_id
	where m.role_id=_role_id
		and p.is_role=false
		and not exists( select 1 from access_role_members o where o.member_id=m.member_id and o.role_id<>_role_id )
		and not exists( select 1 from access_acl a where a.permission_id=m.member_id );

	delete from access_acl where permission_id=_role_id;
	delete from access_role_members where role_id=_role_id or member_id=_role_id;
	delete from access_permission_rights where permission_id=_role_id or permission_id in ( select permission_id from access_role_purge_members );
	delete from access_roles where role_id=_role_id;
	delete from access_permissions where permission_id in ( select permission_id from access_role_purge_members );
	drop temporary table access_role_purge_members;
	delete from access_permissions where permission_id=_role_id;
end
