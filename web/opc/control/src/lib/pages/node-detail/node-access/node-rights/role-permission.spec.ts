import { Rights } from 'jde-access';
import { RolePermissionRow, toRolePermissions } from './role-permission';

//reviews-issue/historian-group-access.md #13, #14:  the one mapping NodeAccess and a historian group's Access tab both read their roles
//query through, from its typed rows to the grid's
describe( 'toRolePermissions', ()=>{
	it( "maps a role's permission, and a role without one, sorted by name", ()=>{
		const rows:RolePermissionRow[] = [
			{id: 7, name: 'ops', permissionRight: {id: 3, allowed: ['Read', 'Update'], denied: ['Delete'], resource: {id: 9, criteria: 'ns=4;i=6020'}}},
			{id: 8, name: 'engineer'}
		];
		expect( toRolePermissions( rows, 'ns=4;i=1' ) ).toEqual( [
			{roleId: 8, roleName: 'engineer', permissionId: 0, allowed: Rights.None, denied: Rights.None, resourceId: 0, criteria: 'ns=4;i=1'},
			{roleId: 7, roleName: 'ops', permissionId: 3, allowed: Rights.Read|Rights.Update, denied: Rights.Delete, resourceId: 9, criteria: 'ns=4;i=6020'}
		] );
	} );
} );