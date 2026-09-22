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
import { HttpErrorResponse, provideHttpClient } from '@angular/common/http';
import { vi } from 'vitest';
import { IENVIRONMENT } from 'jde-spa';
import { AUTH_STORE, AuthStore } from 'jde-framework';
import { AccessService } from './access-service';
import { EffectiveRight } from '../model/effective-right';

//reviews/m3-closing.md #13:  the resources list was cached for the page's life, and it carries each resource's enforcement
//(`deleted`).  Effective rights builds its lockout rows from it, so after the Enforced toggle the tab said "nothing
//restricts this user" about a table that now refused them - or kept a lockout row on one that was open to all - until a reload.
describe( 'AccessService resources', ()=>{
	let service:AccessService;
	let queries:string[];
	let enforced:boolean;//the `users` resource's state on the server
	let nodes:object[];//node-scoped resource rows
	beforeEach( ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			provideHttpClient(),
			{ provide: IENVIRONMENT, useValue: {get: ()=>undefined} },
			{ provide: AUTH_STORE, useValue: {user: ()=>undefined, logout: ()=>{}} as unknown as AuthStore }
		]});
		service = TestBed.inject( AccessService );
		queries = [];
		enforced = false;
		nodes = [];
		vi.spyOn( service, 'queryArray' ).mockImplementation( (async ( ql:string )=>{
			queries.push( ql.split('(')[0].split('{')[0].trim() );
			if( ql.startsWith("resources") ){
				const all = [{id: 7, schemaName: "access", slug: "users", name: "users", allowed: 255, deleted: enforced ? null : "2026-09-21T00:00:00Z"}, ...nodes];
				return ql.includes( "criteria:null" ) ? all.filter( r=>!(r as any).criteria ) : all;//as the server filters
			}
			return [];//userRights - the user holds no grant - and the group and role names
		}) as any );
		vi.spyOn( service as any, 'postQL' ).mockImplementation( (async ( ql:string )=>{
			enforced = ql.includes( "restoreResource" );
			return {};
		}) as any );
	} );
	const lockouts = async ()=>(await service.effectiveRights( 2 )).map( r=>r.resource.slug );//the user holds no grant, so every row is a lockout row

	it( 'shows a lockout once a resource is enforced, in the same session', async ()=>{
		expect( await lockouts() ).toEqual( [] );//unenforced:  nothing restricts the user
		await service.mutate( `restoreResource( id:7 )` );
		expect( await lockouts() ).toEqual( ["users"] );
	} );

	it( 'drops the lockout once it is not', async ()=>{
		enforced = true;
		expect( await lockouts() ).toEqual( ["users"] );
		await service.mutate( `deleteResource( id:7 )` );
		expect( await lockouts() ).toEqual( [] );
	} );

	it( 'reads the resources fresh for the tab, whoever changed them', async ()=>{//another admin, another tab
		await service.effectiveRights( 2 );
		enforced = true;
		expect( await lockouts() ).toEqual( ["users"] );
		expect( queries.filter(q=>q=="resources") ).toHaveLength( 2 );
	} );

	it( 'still keeps one load for the pages that share it', async ()=>{
		await Promise.all( [service.loadResources(), service.loadResources()] );
		expect( queries.filter(q=>q=="resources") ).toHaveLength( 1 );
	} );

	it( 'never keeps a failed load, fresh or not', async ()=>{//angular-review2 #14 / angular-review3 L5
		vi.mocked( service.queryArray ).mockRejectedValueOnce( new Error("down") );
		await expect( service.effectiveRights(2) ).rejects.toThrow( "down" );
		expect( await service.loadResources() ).toHaveLength( 1 );
	} );

	//reviews/m3-closing.md #14:  the tab's resources were the table rows only (criteria:null), so a node the user is locked out
	//of never showed - "nothing restricts this user" while the OpcServer denied the subtree.
	it( 'shows a lockout on a node resource the user holds nothing on', async ()=>{
		nodes = [{id: 32, schemaName: "opc.install", slug: "nodeIds", name: "nodeIds", criteria: "ns=4;i=6030", deleted: null}];
		expect( (await service.effectiveRights(2)).map(r=>[r.resource.slug, r.resource.criteria, r.isLockout]) ).toEqual( [["nodeIds", "ns=4;i=6030", true]] );
	} );

	//reviews/m3-closing.md #28:  the server lets a user read their own rights whatever they hold, but the tab also asked for the
	//group and role names - and a 403 on `roles` (enforced, no Read) failed the whole tab, the one place that would say why.
	//The names only label the paths;  without them a path reads by id.  The rights and the resources stay fatal.
	it( "still shows the user's rights when the role names are refused", async ()=>{
		enforced = true;
		const grant = {resource: {id: 7, schemaName: "access", slug: "users", criteria: "", deleted: null}, allowed: 1, denied: 0, effective: 1, sources: [{permissionId: 5, allowed: 1, denied: 0, path: [{id: 11, type: "role"}]}]};
		const answer = vi.mocked( service.queryArray ).getMockImplementation()!;
		vi.mocked( service.queryArray ).mockImplementation( (async ( ql:string, vars:any )=>{
			if( ql.startsWith("roles") )
				throw new HttpErrorResponse( {status: 403, error: "[bob]User does not have 'Read' access to 'roles'."} );
			return ql.startsWith( "userRights" ) ? [grant] : answer( ql, vars );
		}) as any );
		const warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		const rights = await service.effectiveRights( 2 );
		expect( rights.map(r=>EffectiveRight.describe(r.sources[0])) ).toEqual( ["role #11"] );
		expect( warn ).toHaveBeenCalled();//said, not swallowed
		warn.mockRestore();
	} );

	it( 'still fails the tab when the rights themselves cannot be read', async ()=>{
		const answer = vi.mocked( service.queryArray ).getMockImplementation()!;
		vi.mocked( service.queryArray ).mockImplementation( (async ( ql:string, vars:any )=>{
			if( ql.startsWith("userRights") )
				throw new HttpErrorResponse( {status: 500} );
			return answer( ql, vars );
		}) as any );
		await expect( service.effectiveRights(2) ).rejects.toBeInstanceOf( HttpErrorResponse );
	} );
} );
