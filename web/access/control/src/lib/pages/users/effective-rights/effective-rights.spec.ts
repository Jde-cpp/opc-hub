import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { ACCESS_SERVICE } from '../../../services/access-service';
import { EffectiveRight, RightsAnswer } from '../../../model/effective-right';
import { NODE_LINK_RESOLVER, NodeLinkResolver } from '../../../model/node-link';
import { Rights } from '../../../model/permission';
import { Resource } from '../../../model/resource';
import { EffectiveRights } from './effective-rights';
import { snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

const groups = new EffectiveRight( {
	resource: new Resource( {id: 12, schemaName: "access", name: "groups", slug: "groups", allowed: Rights.All} ),
	allowed: Rights.Read|Rights.Update, denied: Rights.Update,
	sources: [
		{ permissionId: 1, allowed: Rights.Read|Rights.Update, denied: Rights.None, path: [{id: 11, type: "role", name: "Viewer"}] },
		{ permissionId: 2, allowed: Rights.None, denied: Rights.Update, path: [] }
	]
} );
const users = new EffectiveRight( { resource: new Resource({id: 13, schemaName: "access", name: "users", slug: "users", allowed: Rights.All}) } );//enforced, nothing granted

const pathLists = { groups: [{id: 21, name: "Ops"}], roles: [] };
//asked:  the service calls the component made;  handed:  the lists each call was handed
const create = ( rights:EffectiveRight[], failWith?:Error, nodeLinks?:NodeLinkResolver, input:"userId"|"groupId"="userId" )=>{
	const service = vi.fn( async ( _call:string, _id:number, _lists?:unknown )=>{ if( failWith ) throw failWith; return { rights, lists: pathLists }; } );
	TestBed.configureTestingModule({ providers: [
		provideRouter( [] ),//a resolved link renders as routerLink
		{ provide: ACCESS_SERVICE, useValue: { effectiveRights: ( id:number, lists?:unknown )=>service( "effectiveRights", id, lists ), groupRights: ( id:number, lists?:unknown )=>service( "groupRights", id, lists ) } },
		{ provide: SnackbarService, useValue: snackbarStub() },
		...(nodeLinks ? [{ provide: NODE_LINK_RESOLVER, useValue: nodeLinks }] : [])
	]});
	const fixture = TestBed.createComponent( EffectiveRights );
	fixture.componentRef.setInput( "holder", input=="groupId" ? {groupId: 7} : {userId: 7} );
	return { fixture, asked: ()=>service.mock.calls.map( ([call, id])=>`${call}(${id})` ), handed: ()=>service.mock.calls.map( c=>c[2] ) };
};

describe( 'EffectiveRights', ()=>{
	it( 'loads the user\'s rows and marks the cells from the effective and denied bits', async ()=>{
		const { fixture, asked } = create( [groups, users] );
		await stable( fixture, 1 );
		const page = fixture.componentInstance;
		expect( asked() ).toEqual( ["effectiveRights(7)"] );
		expect( page.isLoading() ).toBe( false );
		expect( page.rows() ).toHaveLength( 2 );
		expect( page.isEffective(groups, Rights.Read) ).toBe( true );
		expect( page.isEffective(groups, Rights.Update) ).toBe( false );
		expect( page.isDenied(groups, Rights.Update) ).toBe( true );
		expect( page.tooltip(groups, Rights.Update) ).toBe( "Allowed - role Viewer\nDenied - direct grant" );
		expect( page.tooltip(users, Rights.Read) ).toContain( "every action is denied" );
	} );

	it( 'loads a group\'s rows through groupRights, with its own empty text', async ()=>{
		const { fixture, asked } = create( [], undefined, undefined, "groupId" );
		await stable( fixture, 2 );
		expect( asked() ).toEqual( ["groupRights(7)"] );
		expect( fixture.componentInstance.isGroup() ).toBe( true );
		expect( (fixture.nativeElement as HTMLElement).querySelector(".empty")?.textContent ).toContain( "Its members get nothing through this group" );
	} );

	//reviews-issue/252-effective-rights-group.md #5:  an empty answer is also a deleted identity's, so neither text gives a reason only a live one has
	it( 'says a user with no rows has nothing enforced and no grant', async ()=>{
		const { fixture } = create( [] );
		await stable( fixture, 2 );
		expect( (fixture.nativeElement as HTMLElement).querySelector(".empty")?.textContent ).toContain( "No resource is enforced and no grant reaches this user" );
	} );

	it( 'shows one column per right, without the editable grid\'s None and All shortcuts', ()=>{
		const page = create( [] ).fixture.componentInstance;
		expect( page.displayedColumns.slice(0, 3) ).toEqual( ["schema", "resource", "enforced"] );
		expect( page.displayedColumns ).not.toContain( "None" );
		expect( page.displayedColumns ).not.toContain( "All" );
		expect( page.displayedColumns ).toContain( "Administer" );
	} );

	it( 'folds node rows under their table row until it is expanded', async ()=>{
		const table = new EffectiveRight( { resource: new Resource({id: 20, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", allowed: Rights.All}) } );
		const nodeRows = ["ns=4;i=6010", "ns=4;i=6020"].map( (criteria, i)=>new EffectiveRight({ resource: new Resource({id: 30+i, schemaName: "opc.debug", name: "nodeIds", slug: "nodeIds", criteria, allowed: Rights.All}), allowed: Rights.Read, sources: [{permissionId: 5, allowed: Rights.Read, denied: Rights.None, path: []}] }) );
		const { fixture } = create( EffectiveRight.nest([table, ...nodeRows]) );
		await stable( fixture, 1 );
		const page = fixture.componentInstance;
		expect( page.displayRows() ).toEqual( [table] );
		expect( page.childrenLabel(table) ).toBe( "2 nodes" );
		page.toggle( table );
		expect( page.displayRows().map(r=>r.resource.id) ).toEqual( [20, 30, 31] );
		expect( page.isExpanded(table) ).toBe( true );
		page.toggle( table );
		expect( page.displayRows() ).toEqual( [table] );
	} );

	//The link is the app's to provide (only jde-opc can place a node):  asked once per child when its root first opens, a node
	//the resolver cannot place - or a resolver that throws, or none at all - leaves the criteria as text.
	it( 'links a node row to its page through the resolver, once its root is expanded', async ()=>{
		const table = new EffectiveRight( { resource: new Resource({id: 20, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", allowed: Rights.All}) } );
		const placed = new EffectiveRight( { resource: new Resource({id: 30, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", criteria: "ns=5;i=5005", allowed: Rights.All}), allowed: Rights.Read } );
		const lost = new EffectiveRight( { resource: new Resource({id: 31, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", criteria: "ns=5;i=9", allowed: Rights.All}), allowed: Rights.Read } );
		const resolve = vi.fn<NodeLinkResolver["resolve"]>( async ( _schema, criteria )=>criteria=="ns=5;i=5005" ? { route: ['/gateways', 'gw', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" } : undefined );
		const { fixture } = create( EffectiveRight.nest([table, placed, lost]), undefined, {resolve} );
		await stable( fixture, 1 );
		const page = fixture.componentInstance;
		expect( resolve ).not.toHaveBeenCalled();//nothing asked while collapsed
		page.toggle( table );
		await stable( fixture, 1 );
		expect( page.link(placed)?.route ).toEqual( ['/gateways', 'gw', 'local', 'pumps', 'pump1'] );
		expect( page.link(placed)?.name ).toBe( "Pump 1" );//the link says the name; the path and criteria are its title
		expect( page.linkTitle(page.link(placed)!, placed) ).toBe( "pumps/pump1 - ns=5;i=5005" );
		expect( page.link(lost) ).toBeUndefined();
		page.toggle( table ); page.toggle( table );
		await stable( fixture, 1 );
		expect( resolve.mock.calls.map( ([schema, criteria])=>`${schema}/${criteria}` ) ).toEqual( ["opc.debug/ns=5;i=5005", "opc.debug/ns=5;i=9"] );//once each, not per expand
	} );

	it( 'shows the criteria as text when the app provides no resolver', async ()=>{
		const table = new EffectiveRight( { resource: new Resource({id: 20, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", allowed: Rights.All}) } );
		const node = new EffectiveRight( { resource: new Resource({id: 30, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", criteria: "ns=5;i=5005", allowed: Rights.All}), allowed: Rights.Read } );
		const { fixture } = create( EffectiveRight.nest([table, node]) );
		await stable( fixture, 1 );
		fixture.componentInstance.toggle( table );
		await stable( fixture, 1 );
		expect( fixture.componentInstance.link(node) ).toBeUndefined();
	} );

	//reviews-issue/252-effective-rights-group.md #6:  the router reuses the page for a sibling record, so one instance loads several.
	it( 'collapses its rows when the record changes', async ()=>{
		const { fixture, asked } = create( [groups, users], undefined, undefined, "groupId" );
		await stable( fixture, 1 );
		const page = fixture.componentInstance;
		page.toggle( groups );
		expect( page.isExpanded(groups) ).toBe( true );
		fixture.componentRef.setInput( "holder", {groupId: 8} );
		await stable( fixture, 1 );
		expect( asked() ).toEqual( ["groupRights(7)", "groupRights(8)"] );
		expect( page.isExpanded(groups) ).toBe( false );
	} );

	//reviews-issue/252-effective-rights-group.md #15:  the note lives in the tab's own empty slot, and the server is not asked
	it( 'says why a deleted group shows nothing, and asks the server nothing', async ()=>{
		const { fixture, asked } = create( [groups], undefined, undefined, "groupId" );
		fixture.componentRef.setInput( "deleted", true );
		await stable( fixture, 1 );
		expect( asked() ).toEqual( [] );
		expect( (fixture.nativeElement as HTMLElement).querySelector(".empty")?.textContent ).toContain( "A deleted group passes nothing on" );
	} );

	it( 'says why a deleted user shows nothing, and asks the server nothing', async ()=>{
		const { fixture, asked } = create( [groups] );
		fixture.componentRef.setInput( "deleted", true );
		await stable( fixture, 1 );
		expect( asked() ).toEqual( [] );
		expect( (fixture.nativeElement as HTMLElement).querySelector(".empty")?.textContent ).toContain( "A deleted user is refused everything" );
	} );

	//reviews-issue/252-effective-rights-group.md #12:  the name lists are read once, not per record
	it( 'hands the next record the names the last one read', async ()=>{
		const { fixture, asked, handed } = create( [groups], undefined, undefined, "groupId" );
		await stable( fixture, 1 );
		fixture.componentRef.setInput( "holder", {userId: 8} );
		await stable( fixture, 1 );
		expect( asked() ).toEqual( ["groupRights(7)", "effectiveRights(8)"] );
		expect( handed() ).toEqual( [undefined, pathLists] );
	} );

	it( 'drops the failure of a load a newer one overtook', async ()=>{
		const pending = new Map<number, {resolve:( answer:RightsAnswer )=>void, reject:( e:unknown )=>void}>();
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [
			provideRouter( [] ),
			{ provide: ACCESS_SERVICE, useValue: { groupRights: ( id:number )=>new Promise<RightsAnswer>( (resolve, reject)=>pending.set(id, {resolve, reject}) ) } },
			{ provide: SnackbarService, useValue: snackbar }
		]});
		const fixture = TestBed.createComponent( EffectiveRights );
		fixture.componentRef.setInput( "holder", {groupId: 7} );
		fixture.detectChanges();
		fixture.componentRef.setInput( "holder", {groupId: 8} );
		fixture.detectChanges();
		pending.get( 8 )!.resolve( {rights: [groups], lists: {}} );
		await stable( fixture, 1 );
		pending.get( 7 )!.reject( new Error("slow, and failed") );
		await stable( fixture, 1 );
		expect( snackbar.exception ).not.toHaveBeenCalled();
		expect( fixture.componentInstance.isLoading() ).toBe( false );
		expect( fixture.componentInstance.rows() ).toEqual( [groups] );
	} );

	it( 'keeps the table hidden when the load fails', async ()=>{
		const { fixture } = create( [], new Error("nope") );
		await stable( fixture, 1 );
		expect( fixture.componentInstance.isLoading() ).toBe( true );
	} );
} );