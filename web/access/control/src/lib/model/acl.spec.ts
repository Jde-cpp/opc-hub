import { MutationType } from 'jde-framework';
import { Acl } from './acl';
import { Group } from './group';
import { Role } from './role';
import { User } from './user';

//A20:  the acl split and the four add/remove diffs, on one helper each.
describe( 'Acl', ()=>{
	it( 'splits an acl into roles and an identity\'s own permissions', ()=>{
		const {roles, permissions} = Acl.split( [{role: {id: 3, name: 'ops'}}, {permissionRight: {id: 9, allowed: 2, denied: 0, resource: {id: 4}}}, {}] );
		expect( roles.map( r=>r.id ) ).toEqual( [3] );
		expect( permissions.map( p=>p.id ) ).toEqual( [9] );
		expect( new Group( {id: 1, acl: [{role: {id: 3}}]} ).roles.map( r=>r.id ) ).toEqual( [3] );
		expect( new User( {id: 1, acl: [{role: {id: 4}}]} ).roles.map( r=>r.id ) ).toEqual( [4] );
	} );

	it( 'purges the rows a role dropped and creates the ones it gained, the role on each', ()=>{
		const original = new Role( {id: 5, slug: 'ops', name: 'Ops', groups: [{id: 1}, {id: 2}], users: [{id: 10}], childRoles: [{id: 7}], permissions: []} );
		const edited = new Role( {...original, groups: [{id: 2}, {id: 3}], users: [{id: 10}], childRoles: [{id: 8}]} );
		const rows = edited.mutation( original ).map( m=>[m.type, m.id, m.args] );//an acl row has no id of its own:  0
		expect( rows ).toEqual( [
			[MutationType.Remove, 5, {role: {id: [7]}}],
			[MutationType.Add, 5, {role: {id: [8]}}],
			[MutationType.Purge, 0, {identity: {id: 1}, role: {id: 5}}],
			[MutationType.Create, 0, {identity: {id: 3}, role: {id: 5}}]
		] );
	} );

	it( 'gives a user\'s roles and groups the same diff, by role and by group', ()=>{
		const original = new User( {id: 10, slug: 'jane', name: 'Jane', roles: [{id: 5}], groups: [{id: 1}], permissions: []} );
		const edited = new User( {...original, roles: [{id: 6}], groups: [{id: 1}, {id: 2}]} );
		const rows = edited.mutation( original ).map( m=>[m.type, m.id, m.args] );
		expect( rows ).toEqual( [
			[MutationType.Add, 2, {memberId: 10}],
			[MutationType.Purge, 0, {identity: {id: 10}, role: {id: 5}}],
			[MutationType.Create, 0, {identity: {id: 10}, role: {id: 6}}]
		] );
	} );
} );