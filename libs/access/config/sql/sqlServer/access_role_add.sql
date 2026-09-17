create or alter proc [dbo].access_role_add( @role_id int, @allowed bigint, @denied bigint, @resourceSlug varchar(32), @schema varchar(32), @resourceName varchar(64), @criteria varchar(672), @permission_id int output ) as begin
	set nocount on;
	declare @resource_id smallint;
	select @resource_id = resource_id
	from access_resources
	where slug=@resourceSlug
		and schema_name = coalesce(@schema, schema_name)
		and criteria is not distinct from @criteria;
	if @resource_id is null begin
		-- install-issues #25: a *root* (criteria-null) resource that exists only because a role referenced it (the seed's addRole
		-- on opc.install nodeIds, run before the OpcServer declares it) ships deleted, i.e. unenforced - creating it enforced closed
		-- node access on a fresh install.  Unenforced is the shipped default (ResourceSyncAwait creates then deletes each one); the
		-- grant applies once an operator enforces it.  A criteria-scoped row is a deliberate per-node grant and stays enforced.
		insert into access_resources( slug, schema_name, name, criteria, deleted ) values( @resourceSlug, @schema, coalesce(@resourceName, @resourceSlug), @criteria, case when @criteria is null then getutcdate() else null end );
		set @resource_id = scope_identity();
	end;

	select @permission_id=permission_id
	from access_role_members members
		join access_permission_rights rights on members.member_id=rights.permission_id
	where members.role_id=@role_id
		and rights.resource_id=@resource_id;

	if @permission_id is not null begin
		update access_permission_rights set allowed=@allowed, denied=@denied
		where permission_id=@permission_id;
	end else begin
		insert into access_permissions( is_role ) values( 0 );
		set @permission_id = scope_identity();

		insert into access_permission_rights( permission_id, allowed, denied, resource_id )
		values( @permission_id, @allowed, @denied, @resource_id );

		insert into access_role_members( role_id, member_id ) values( @role_id, @permission_id );
	end;
end