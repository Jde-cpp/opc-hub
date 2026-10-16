import { Mutation, MutationType } from 'jde-framework';
import { Permission, Role } from 'jde-access';

//The rights grid's row (NodeRights) and what the pages that fill it share - NodeAccess, and a historian group's Access tab
//(hist-group-access.ts) - here rather than in either page, so neither imports the other's component to reach them
//(reviews-issue/historian-group-access.md #13).
export type RolePermission = {
	roleId: number;
	roleName: string;
	permissionId: number;
	allowed:number;
	denied:number;
	resourceId: number;
	criteria: string;
	inherited?: string;//what the role holds through the row governing the owner, where that isn't the row the grid grants on:  the Inherited column, its tooltip this
};
//A role as the pages' roles query answers it:  `permissionRight` only where it holds a permission on the resource asked for, its
//allowed and denied [Right] name arrays (#14)
export type RolePermissionRow = { id:number; name:string; permissionRight?:{ id:number; allowed:string[]; denied:string[]; resource?:{ id:number; criteria?:string|null } } };
//The grid's rows from that answer, by name;  `criteria` is the resource's where the row doesn't carry it
export function toRolePermissions( rows:RolePermissionRow[], criteria:string ):RolePermission[]{
	return rows.map( role=>{
		const pr = role.permissionRight;
		return { roleId: role.id, roleName: role.name, permissionId: pr?.id ?? 0, allowed: Permission.toRights( pr?.allowed ?? [] ), denied: Permission.toRights( pr?.denied ?? [] ), resourceId: pr?.resource?.id ?? 0, criteria: pr?.resource?.criteria ?? criteria };
	} ).sort( ( a, b )=>a.roleName.localeCompare( b.roleName ) );
}

export function isDenied( role:RolePermission, rights:number ):boolean{
	return rights!=0 && ((role.denied ?? 0) & rights)==rights;
}
//the None column (rights 0) is allowed where the role holds nothing
export function isAllowed( role:RolePermission, rights:number ):boolean{
	const allowed = role.allowed ?? 0;
	return (rights==0 && !allowed && !role.denied) || (rights!=0 && (allowed & rights)==rights);
}
//A click on a right's cell, as the grid cycles it:  allowed to denied, denied to none, none to allowed;  the None column (rights 0)
//clears both sides.
export function toggleRights( role:RolePermission, rights:number ):void{
	if( isAllowed(role, rights) ){//allowed->denied
		role.allowed &= ~rights;
		role.denied |= rights;
	}
	else if( isDenied(role, rights) )//denied->none
		role.denied &= ~rights;
	else if( rights==0 ){//none->allowed
		role.allowed = 0;
		role.denied = 0;
	}
	else
		role.allowed |= rights;
}
//The mutation that takes a role from the server's row to its edited one, or none where the row is the server's again.  Remove-vs-Add is
//decided from the role's RESULTING rights, never from the side that changed:  clearing one half of a two-sided permission (allowed=Read,
//denied=Write -> clear the denied Write) used to look like "no rights left" and removed the whole permissionRight, silently dropping the
//still-wanted allowed=Read.  Permission.roleMutations evaluates the resulting row; match it.  `resource` names the row an Add grants on.
export function roleMutation( role:RolePermission, original:RolePermission, resource:()=>Record<string,unknown> ):Mutation|undefined{
	if( role.allowed==original.allowed && role.denied==original.denied )
		return undefined;
	if( !role.allowed && !role.denied )
		return role.permissionId ? new Mutation( Role.typeName, role.roleId, {permissionRight: {id: role.permissionId}}, MutationType.Remove ) : undefined;//permissionId 0 = nothing on the server to remove (a pending Add is dropped by returning none)
	return new Mutation( Role.typeName, role.roleId, {permissionRight: {allowed: role.allowed, denied: role.denied, resource: resource()}}, MutationType.Add );
}
//the pending batch with `mutation` in the place of the role's earlier one, or without one where the role is back to the server's row
export function withMutation( mutations:Mutation[], roleId:number, mutation:Mutation|undefined ):Mutation[]{
	const index = mutations.findIndex( m=>m.id==roleId );
	if( index<0 )
		return mutation ? [...mutations, mutation] : mutations;
	const y = mutations.slice();
	if( mutation )
		y[index] = mutation;
	else
		y.splice( index, 1 );
	return y;
}