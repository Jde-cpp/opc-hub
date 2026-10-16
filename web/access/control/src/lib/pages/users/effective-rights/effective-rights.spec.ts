import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { ACCESS_SERVICE } from '../../../services/access-service';
import { EffectiveRight } from '../../../model/effective-right';
import { NODE_LINK_RESOLVER, NodeLinkResolver } from '../../../model/node-link';
import { Rights } from '../../../model/permission';
import { Resource } from '../../../model/resource';
import { EffectiveRights } from './effective-rights';

const groups = new EffectiveRight( {
	resource: new Resource( {id: 12, schemaName: "access", name: "groups", slug: "groups", allowed: Rights.All} ),
	allowed: Rights.Read|Rights.Update, denied: Rights.Update,
	sources: [
		{ permissionId: 1, allowed: Rights.Read|Rights.Update, denied: Rights.None, path: [{id: 11, type: "role", name: "Viewer"}] },
		{ permissionId: 2, allowed: Rights.None, denied: Rights.Update, path: [] }
	]
} );
const users = new EffectiveRight( { resource: new Resource({id: 13, schemaName: "access", name: "users", slug: "users", allowed: Rights.All}) } );//enforced, nothing granted

let asked:string[];//the service calls the component made
const create = ( rights:EffectiveRight[], failWith?:Error, nodeLinks?:NodeLinkResolver, input:"userId"|"groupId"="userId" )=>{
	asked = [];
	const answer = ( call:string )=>async ( id:number )=>{ asked.push( `${call}(${id})` ); if( failWith ) throw failWith; return rights; };
	TestBed.configureTestingModule({ providers: [
		provideRouter( [] ),//a resolved link renders as routerLink
		{ provide: ACCESS_SERVICE, useValue: { effectiveRights: answer("effectiveRights"), groupRights: answer("groupRights") } },
		{ provide: SnackbarService, useValue: { exception: ()=>{} } },
		...(nodeLinks ? [{ provide: NODE_LINK_RESOLVER, useValue: nodeLinks }] : [])
	]});
	const fixture = TestBed.createComponent( EffectiveRights );
	fixture.componentRef.setInput( input, 7 );
	return fixture;
};

describe( 'EffectiveRights', ()=>{
	it( 'loads the user\'s rows and marks the cells from the effective and denied bits', async ()=>{
		const fixture = create( [groups, users] );
		fixture.detectChanges();
		await fixture.whenStable();
		const page = fixture.componentInstance;
		expect( asked ).toEqual( ["effectiveRights(7)"] );
		expect( page.isLoading() ).toBe( false );
		expect( page.rows() ).toHaveLength( 2 );
		expect( page.isEffective(groups, Rights.Read) ).toBe( true );
		expect( page.isEffective(groups, Rights.Update) ).toBe( false );
		expect( page.isDenied(groups, Rights.Update) ).toBe( true );
		expect( page.tooltip(groups, Rights.Update) ).toBe( "Allowed - role Viewer\nDenied - direct grant" );
		expect( page.tooltip(users, Rights.Read) ).toContain( "every action is denied" );
	} );

	it( 'loads a group\'s rows through groupRights, with its own empty text', async ()=>{
		const fixture = create( [], undefined, undefined, "groupId" );
		fixture.detectChanges();
		await fixture.whenStable();
		fixture.detectChanges();
		expect( asked ).toEqual( ["groupRights(7)"] );
		expect( fixture.componentInstance.isGroup() ).toBe( true );
		expect( (fixture.nativeElement as HTMLElement).querySelector(".empty")?.textContent ).toContain( "its members get nothing through it" );
	} );

	it( 'shows one column per right, without the editable grid\'s None and All shortcuts', ()=>{
		const page = create( [] ).componentInstance;
		expect( page.displayedColumns.slice(0, 3) ).toEqual( ["schema", "resource", "enforced"] );
		expect( page.displayedColumns ).not.toContain( "None" );
		expect( page.displayedColumns ).not.toContain( "All" );
		expect( page.displayedColumns ).toContain( "Administer" );
	} );

	it( 'folds node rows under their table row until it is expanded', async ()=>{
		const table = new EffectiveRight( { resource: new Resource({id: 20, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", allowed: Rights.All}) } );
		const nodeRows = ["ns=4;i=6010", "ns=4;i=6020"].map( (criteria, i)=>new EffectiveRight({ resource: new Resource({id: 30+i, schemaName: "opc.debug", name: "nodeIds", slug: "nodeIds", criteria, allowed: Rights.All}), allowed: Rights.Read, sources: [{permissionId: 5, allowed: Rights.Read, denied: Rights.None, path: []}] }) );
		const fixture = create( EffectiveRight.nest([table, ...nodeRows]) );
		fixture.detectChanges();
		await fixture.whenStable();
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
		const asked:string[] = [];
		const resolver:NodeLinkResolver = { resolve: async ( schema, criteria )=>{ asked.push( `${schema}/${criteria}` ); return criteria=="ns=5;i=5005" ? { route: ['/gateways', 'gw', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" } : undefined; } };
		const fixture = create( EffectiveRight.nest([table, placed, lost]), undefined, resolver );
		fixture.detectChanges();
		await fixture.whenStable();
		const page = fixture.componentInstance;
		expect( asked ).toEqual( [] );//nothing asked while collapsed
		page.toggle( table );
		await fixture.whenStable();
		expect( page.link(placed)?.route ).toEqual( ['/gateways', 'gw', 'local', 'pumps', 'pump1'] );
		expect( page.link(placed)?.name ).toBe( "Pump 1" );//the link says the name; the path and criteria are its title
		expect( page.linkTitle(page.link(placed)!, placed) ).toBe( "pumps/pump1 - ns=5;i=5005" );
		expect( page.link(lost) ).toBeUndefined();
		page.toggle( table ); page.toggle( table );
		await fixture.whenStable();
		expect( asked ).toEqual( ["opc.debug/ns=5;i=5005", "opc.debug/ns=5;i=9"] );//once each, not per expand
	} );

	it( 'shows the criteria as text when the app provides no resolver', async ()=>{
		const table = new EffectiveRight( { resource: new Resource({id: 20, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", allowed: Rights.All}) } );
		const node = new EffectiveRight( { resource: new Resource({id: 30, schemaName: "opc.debug", name: "node_ids", slug: "nodeIds", criteria: "ns=5;i=5005", allowed: Rights.All}), allowed: Rights.Read } );
		const fixture = create( EffectiveRight.nest([table, node]) );
		fixture.detectChanges();
		await fixture.whenStable();
		fixture.componentInstance.toggle( table );
		await fixture.whenStable();
		expect( fixture.componentInstance.link(node) ).toBeUndefined();
	} );

	it( 'keeps the table hidden when the load fails', async ()=>{
		const fixture = create( [], new Error("nope") );
		fixture.detectChanges();
		await fixture.whenStable();
		expect( fixture.componentInstance.isLoading() ).toBe( true );
	} );
} );
