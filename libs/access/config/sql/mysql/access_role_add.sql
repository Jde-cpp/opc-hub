drop procedure if exists access_role_add;
go

create procedure access_role_add( _role_id int unsigned, _allowed bigint unsigned, _denied bigint unsigned, _resourceSlug varchar(32), _schema varchar(32), _resourceName varchar(64), _criteria varchar(672), out _permission_id int unsigned )
begin
	declare _resource_id smallint unsigned;
	select resource_id
	into _resource_id
	from access_resources
	where slug=_resourceSlug
		and schema_name = coalesce(_schema, schema_name)
		and criteria <=> _criteria;
	if _resource_id is null then
		-- install-issues #25: a *root* (criteria-null) resource that exists only because a role referenced it (the seed's addRole
		-- on opc.install nodeIds, run before the OpcServer declares it) ships deleted, i.e. unenforced - creating it enforced closed
		-- node access on a fresh install.  Unenforced is the shipped default (ResourceSyncAwait creates then deletes each one); the
		-- grant applies once an operator enforces it.  A criteria-scoped row is a deliberate per-node grant and stays enforced.
		insert into access_resources( slug, schema_name, name, criteria, deleted ) values( _resourceSlug, _schema, coalesce(_resourceName, _resourceSlug), _criteria, case when _criteria is null then CURRENT_TIMESTAMP else null end );
		set _resource_id = LAST_INSERT_ID();
	end if;
	select permission_id
	into _permission_id
	from access_role_members members
		join access_permission_rights rights on members.member_id=rights.permission_id
	where members.role_id=_role_id
		and rights.resource_id=_resource_id;

	if _permission_id is not null then
		update access_permission_rights set allowed=_allowed, denied=_denied
		where permission_id=_permission_id;
	else
		insert into access_permissions( is_role ) values( false );
		set _permission_id = LAST_INSERT_ID();

		insert into access_permission_rights( permission_id, allowed, denied, resource_id )
		values( _permission_id, _allowed, _denied, _resource_id );

		insert into access_role_members( role_id, member_id ) values( _role_id, _permission_id );
	end if;
end
