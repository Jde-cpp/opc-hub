if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute } from '@angular/router';
import { NEVER } from 'rxjs';
import { AppService, MutationType, SnackbarService } from 'jde-framework';
import { Rights } from 'jde-access';
import { NodeAccess } from './node-access';
import { RolePermission } from './node-rights/role-permission';

const row = ( allowed:number, denied:number, permissionId=3 ):RolePermission =>
	({roleId: 5, roleName: "ops", permissionId, allowed, denied, resourceId: 9, criteria: "ns=4;i=6020"});

//angular-review3 #2: Remove-vs-Add was decided from `args` - the CHANGED fields - so clearing one side of a two-sided
//permission looked like "no rights left" and deleted the whole permissionRight, taking the other side with it.
describe( 'NodeAccess.onToggle', ()=>{
	let page:NodeAccess;
	beforeEach( ()=>{
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER} },//no load(): the rows are set by hand below
			{ provide: AppService, useValue: {} },
			{ provide: SnackbarService, useValue: {exception: ()=>{}} }
		]});
		const fixture = TestBed.createComponent( NodeAccess );
		fixture.componentRef.setInput( 'accessResource', 'nodes' );
		page = fixture.componentInstance;
	} );
	const start = ( allowed:number, denied:number, permissionId=3 )=>{
		page.original = [ row(allowed, denied, permissionId) ];
		page.roles = [ {...row(allowed, denied, permissionId)} ];
		return page.roles[0];
	};

	it( 'keeps the permissionRight when only the denied side is cleared', ()=>{
		const role = start( Rights.Read, Rights.Update );
		page.onToggle( {role, rights: Rights.Update} );//denied -> none
		expect( role ).toMatchObject( {allowed: Rights.Read, denied: Rights.None} );
		const mutations = page.mutations();
		expect( mutations ).toHaveLength( 1 );
		expect( mutations[0].type ).toBe( MutationType.Add );
		expect( mutations[0].args.permissionRight ).toMatchObject( {allowed: Rights.Read, denied: Rights.None} );
	} );

	it( 'removes the permissionRight when the row really ends with no rights', ()=>{
		const role = start( Rights.Read, Rights.None );
		page.onToggle( {role, rights: Rights.Read} );//allowed -> denied
		page.onToggle( {role, rights: Rights.Read} );//denied -> none
		const mutations = page.mutations();
		expect( mutations ).toHaveLength( 1 );
		expect( mutations[0].type ).toBe( MutationType.Remove );
		expect( mutations[0].args ).toEqual( {permissionRight: {id: 3}} );
	} );

	it( 'the None column clears BOTH sides', ()=>{
		const role = start( Rights.Read, Rights.Update );
		page.onToggle( {role, rights: 0} );
		expect( role ).toMatchObject( {allowed: Rights.None, denied: Rights.None} );
		expect( page.mutations()[0].type ).toBe( MutationType.Remove );
	} );

	it( 'queues nothing to remove for a role that has no permission on the server', ()=>{
		const role = start( Rights.None, Rights.None, 0 );
		page.onToggle( {role, rights: Rights.Read} );//none -> allowed
		expect( page.mutations()[0].type ).toBe( MutationType.Add );
		page.onToggle( {role, rights: Rights.Read} );//allowed -> denied
		page.onToggle( {role, rights: Rights.Read} );//denied -> none, back to the original
		expect( page.mutations() ).toEqual( [] );//the pending Add is dropped, and there is nothing to Remove
	} );
} );
