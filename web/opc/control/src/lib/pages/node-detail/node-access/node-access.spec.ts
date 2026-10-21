import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { AppService, MutationType, SnackbarService } from 'jde-framework';
import { Rights } from 'jde-access';
import { NodeAccess } from './node-access';
import { RolePermission } from './node-rights/role-permission';
import { Variable } from '../../../model/node';
import { stable } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';

const row = ( allowed:number, denied:number, permissionId=3 ):RolePermission=>
	({roleId: 5, roleName: "ops", permissionId, allowed, denied, resourceId: 9, criteria: "ns=4;i=6020"});

//angular-review3 #2: Remove-vs-Add was decided from `args` - the CHANGED fields - so clearing one side of a two-sided
//permission looked like "no rights left" and deleted the whole permissionRight, taking the other side with it.
describe( 'NodeAccess.onToggle', ()=>{
	//the page holding one role's row, and that role
	const create = ( allowed:number, denied:number, permissionId=3 )=>{
		TestBed.configureTestingModule({ providers: [
			{ provide: ComponentFixtureAutoDetect, useValue: false },//no load(): the rows are set by hand below
			{ provide: AppService, useValue: {} },
			{ provide: SnackbarService, useValue: snackbarStub() }
		]});
		const fixture = TestBed.createComponent( NodeAccess );
		fixture.componentRef.setInput( 'accessResource', 'nodes' );
		const page = fixture.componentInstance;
		page.grants.reset( [row(allowed, denied, permissionId)] );
		return { page, role: page.grants.roles()[0] };
	};

	it( 'keeps the permissionRight when only the denied side is cleared', ()=>{
		const { page, role } = create( Rights.Read, Rights.Update );
		page.onToggle( {role, rights: Rights.Update} );//denied -> none
		expect( role ).toMatchObject( {allowed: Rights.Read, denied: Rights.None} );
		const mutations = page.grants.mutations();
		expect( mutations ).toHaveLength( 1 );
		expect( mutations[0].type ).toBe( MutationType.Add );
		expect( mutations[0].args.permissionRight ).toMatchObject( {allowed: Rights.Read, denied: Rights.None} );
	} );

	it( 'removes the permissionRight when the row really ends with no rights', ()=>{
		const { page, role } = create( Rights.Read, Rights.None );
		page.onToggle( {role, rights: Rights.Read} );//allowed -> denied
		page.onToggle( {role, rights: Rights.Read} );//denied -> none
		const mutations = page.grants.mutations();
		expect( mutations ).toHaveLength( 1 );
		expect( mutations[0].type ).toBe( MutationType.Remove );
		expect( mutations[0].args ).toEqual( {permissionRight: {id: 3}} );
	} );

	it( 'the None column clears BOTH sides', ()=>{
		const { page, role } = create( Rights.Read, Rights.Update );
		page.onToggle( {role, rights: 0} );
		expect( role ).toMatchObject( {allowed: Rights.None, denied: Rights.None} );
		expect( page.grants.mutations()[0].type ).toBe( MutationType.Remove );
	} );

	it( 'queues nothing to remove for a role that has no permission on the server', ()=>{
		const { page, role } = create( Rights.None, Rights.None, 0 );
		page.onToggle( {role, rights: Rights.Read} );//none -> allowed
		expect( page.grants.mutations()[0].type ).toBe( MutationType.Add );
		page.onToggle( {role, rights: Rights.Read} );//allowed -> denied
		page.onToggle( {role, rights: Rights.Read} );//denied -> none, back to the original
		expect( page.grants.mutations() ).toEqual( [] );//the pending Add is dropped, and there is nothing to Remove
	} );
} );


//A16:  the node is read through an effect on its input, as HistGroupAccess reads its group - the router reuses the page from node to
//node - and Save sends the batch through RoleGrants, then reads the roles again.
describe( 'NodeAccess load and save', ()=>{
	const node = ( i:number )=>new Variable( {ns: 4, i, name: `n${i}`, browse: {ns: 4, name: `n${i}`}} as any );
	it( 'reads the node it is given, saves the batch and reads again', async ()=>{
		const queryArray = vi.fn( async ( _q:string, _vars:any )=>[{id: 5, name: 'ops'}] );
		const mutate = vi.fn( async ( _m:unknown )=>{} );
		const appService = { queryArray, mutate };
		const asked = ()=>queryArray.mock.calls.map( ([, vars])=>vars );
		TestBed.configureTestingModule({ providers: [
			{ provide: AppService, useValue: appService },
			{ provide: SnackbarService, useValue: snackbarStub() }
		]});
		const fixture = TestBed.createComponent( NodeAccess );
		fixture.componentRef.setInput( 'accessResource', 'nodes' );
		fixture.componentRef.setInput( 'node', node(6020) );
		await stable( fixture, 1 );
		expect( asked() ).toEqual( [{schemaName: 'opc.nodes', slug: 'nodeIds', criteria: 'ns=4;i=6020'}] );
		const page = fixture.componentInstance;
		page.onToggle( {role: page.grants.roles()[0], rights: Rights.Read} );
		await page.save();
		expect( (mutate.mock.calls[0][0] as any[])[0].args.permissionRight ).toEqual( {allowed: Rights.Read, denied: Rights.None, resource: {schemaName: 'opc.nodes', slug: 'nodeIds', criteria: 'ns=4;i=6020'}} );
		expect( page.grants.mutations() ).toEqual( [] );
		expect( queryArray ).toHaveBeenCalledTimes( 2 );
		fixture.componentRef.setInput( 'node', node(6021) );
		await stable( fixture, 1 );
		expect( asked()[2].criteria ).toBe( 'ns=4;i=6021' );
	} );
} );