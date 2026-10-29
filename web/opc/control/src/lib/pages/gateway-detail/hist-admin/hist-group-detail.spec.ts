import { Component, inject, InjectionToken, input } from '@angular/core';
import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { MatTooltip } from '@angular/material/tooltip';
import { By } from '@angular/platform-browser';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { Mutation, Properties } from 'jde-framework';
import { ExNodeId } from '../../../model/ex-node-id';
import { endpointServedQuery } from '../../../model/gateway-endpoint';
import { histThresholdBlank } from '../../../model/hist';
import { HistGroup, HistPick } from '../../../model/hist-admin';
import { HistAdminService } from '../../../services/hist-admin-service';
import { HistoryService } from '../../../services/history-service';
import { OpcStore } from '../../../services/opc-store';
import { HistGroupAccess } from './hist-group-access';
import { HistGroupDetail } from './hist-group-detail';
import { HistNodePickerDialog } from './hist-node-picker-dialog';
import { flush } from 'jde-spa/testing';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';
import { Pumps, pumpsServer } from 'jde-opc/testing';

const BadNodeIdExists = 0x805E0000;
const row = { id: 5, name: 'Pumps', slug: 'pumps', guid: '0f8fad5b-d9cb-469f-a165-70867728950e', serverConnection: {id: 1, name: 'Local'}, histTemplate: {id: 2, name: 'Pump'}, root: '{"nsu":"urn:jde:pumps","i":5001}', maxTimeInterval: 60000 };
//The Access tab, counted:  `made` is told of each one, which a tab kept alive behind another makes once
const ACCESS_MADE = new InjectionToken<()=>void>( 'AccessMade' );
@Component( {selector: 'hist-group-access', template: ''} )
class AccessStub{
	group = input<unknown>();
	constructor(){ inject( ACCESS_MADE )(); }
}
//The page over stubs:  `mutations` reads what Save sent the gateway, the admin service records what the tabs ask and send, and the
//dialog answers a confirmation or the picker's picks.  `render` lets the fixture's first change detection run ngOnInit and draw the
//page, where the rest drive the page by hand.
const servedRpm = { id: 7, nodeId: 'ns=3;i=6012', path: 'Objects/local/pump1/motorRpm' };//member 7 as the endpoint serves it
const create = async ( pageRow:any, render=false, endpoint:unknown={url: 'opc.tcp://hub:4841', served: [servedRpm]}, root:unknown={path: 'pumps/5~pump1'} )=>{
	//query:  endpoint{…}, and nodes( id ){ path }, the root's, which the admin service's own rootPaths sends
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{} ), query: vi.fn( async ( ql:string, _vars?:unknown )=>ql==HistAdminService.rootPathsQuery ? {nodes: [root]} : {endpoint} ) };
	const mutations = ()=>gateway.mutate.mock.calls.flatMap( ([m])=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	const made = vi.fn();
	const admin = {
		connections: vi.fn( async ()=>[{id: 1, slug: 'local', name: 'Local'}, {id: 3, slug: 'plc', name: 'PLC'}] ),
		templates: vi.fn( async ()=>[{id: 2, name: 'Pump'}] ),
		groupMembers: vi.fn( async ()=>[
			{ id: 7, node: new ExNodeId( {i: 6012, nsu: Pumps} ), templateNodeId: 3, thresholds: {...histThresholdBlank, minTimeInterval: '1000'},
				attributes: { displayName: 'motorRpm', description: 'Measured motor speed, rpm.', dataType: new ExNodeId( {nsu: 'http://opcfoundation.org/UA/', i: 11} ), valueRank: -1, accessLevel: 15, euRange: {low: 0, high: 3000}, path: [{nsu: Pumps, name: 'pump1'}, {nsu: Pumps, name: 'motorRpm'}] } },
			{ id: 8, node: new ExNodeId( {i: 2259, nsu: 'http://opcfoundation.org/UA/'} ), templateNodeId: undefined, thresholds: {...histThresholdBlank}, attributes: {} }
		] ),
		templateMembers: vi.fn( async ()=>[{id: 3, templateId: 2, path: [{nsu: Pumps, name: 'motorRpm'}], thresholds: histThresholdBlank}, {id: 4, templateId: 2, path: [{nsu: Pumps, name: 'status'}], thresholds: histThresholdBlank}] ),
		createGroupMember: vi.fn( async ( _g:unknown, _id:number, node:ExNodeId )=>{ if( node.id==6032 ) throw new Error( "'{\"nsu\":\"urn:jde:pumps\",\"i\":6032}' is already in group 'Other' (9) on this connection:  a node belongs to one group per server." ); return 10; } ),
		purgeGroupMember: vi.fn( async ()=>{} ),
		rootPaths: vi.fn( HistAdminService.prototype.rootPaths ),
		resolve: vi.fn( async ()=>[{templateNode: 3, path: [{nsu: Pumps, name: 'motorRpm'}], node: new ExNodeId( {i: 6012, nsu: Pumps} ), status: 0}, {templateNode: 4, path: [{nsu: Pumps, name: 'status'}], status: BadNodeIdExists, holder: {id: 9, name: 'Other'}}] )
	};
	const dialog = dialogStub( true );//a confirmation's answer;  the picker's picks are its own, none until a test gives some
	dialog.answers.set( HistNodePickerDialog, undefined );
	const {data, snackbar} = configureDetailPage( pageRow, {gateway, providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the zoneless fixture's own first change detection would run ngOnInit a second time, onto the row #load nulled
		{ provide: HistAdminService, useValue: admin },
		{ provide: HistoryService, useValue: {} },
		{ provide: OpcStore, useValue: {getConnection: async ()=>pumpsServer} },
		{ provide: MatDialog, useValue: dialog },
		{ provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'} },//a tab switch completes in its pass, not on a 100 ms fallback timer
		{ provide: ACCESS_MADE, useValue: made }
	]} );
	if( render ){
		TestBed.overrideComponent( Properties, {set: {template: '', imports: []}} );//the form reads a whole schema
		TestBed.overrideComponent( HistGroupDetail, {remove: {imports: [HistGroupAccess]}, add: {imports: [AccessStub]}} );
	}
	const fixture = TestBed.createComponent( HistGroupDetail );
	const page = fixture.componentInstance;
	if( render )
		fixture.detectChanges();
	else
		page.ngOnInit();
	await flush( 3 );
	return { page, fixture, mutations, made, admin, snackbar, dialog, data, gateway };
};

describe( 'HistGroupDetail', ()=>{
	//reviews-issue/historian-group-access.md #10:  one ProfileStore index serves every group, so Resolution, which only a group with a
	//template has, goes last, and Access is the third tab either way - before, a group left on Access opened another on Resolution
	const tabs = async ( pageRow:any )=>{
		const d = await create( pageRow, true );
		return [...d.fixture.nativeElement.querySelectorAll( '.mdc-tab__text-label' )].map( ( e:Element )=>e.textContent!.trim() );
	};
	it( 'keeps Access the third tab on a group with a template', async ()=>{
		expect( await tabs( row ) ).toEqual( ['Properties', 'Members', 'Access', 'Resolution'] );
	} );
	it( 'keeps Access the third tab on a group without one', async ()=>{
		expect( await tabs( {...row, histTemplate: null} ) ).toEqual( ['Properties', 'Members', 'Access'] );
	} );
	//#6:  the Access tab was lazy content the tab group destroyed on leaving, and its unsaved ticks with it
	it( 'keeps a tab alive behind another, so a switch away keeps its unsaved state', async ()=>{
		const d = await create( row, true );
		//through the page's tabIndex, which the group's selectedIndex is bound to;  false skips the fixture's no-changes pass, which reads
		//the tab header the switch updates during the pass as a change made after the check
		const show = async ( index:number )=>{
			d.page.tabIndex.set( index );
			d.fixture.detectChanges( false );
			await flush( 3 );
			d.fixture.detectChanges( false );
			expect( d.fixture.nativeElement.querySelector( '.mdc-tab--active' ).textContent.trim() ).toBe( ['Properties', 'Members', 'Access'][index] );
		};
		await show( 2 );
		expect( d.made ).toHaveBeenCalledTimes( 1 );
		await show( 1 );
		await show( 2 );
		expect( d.made ).toHaveBeenCalledTimes( 1 );
	} );
	//a new group:  the connection is the one field the form can't leave, the template and root are optional, and Save sends the
	//source with the thresholds set
	it( 'saves a new group with its source and thresholds', async ()=>{
		const d = await create( {} );
		expect( d.page.isNew ).toBe( true );
		expect( d.page.excludedFields ).toBe( d.page.excludedFields );//one array, not a new one for Properties each check
		expect( d.page.excludedFields ).toContain( 'guid' );
		expect( d.page.gateway.slug ).toBe( 'g' );
		expect( d.page.connections() ).toHaveLength( 2 );
		expect( d.page.group.canSave ).toBe( false );
		d.page.properties.set( new HistGroup( {...d.page.properties(), name: 'Pumps', slug: 'pumps'} ) );
		expect( d.page.group.canSave ).toBe( false );
		d.page.setConnection( 1 );
		expect( d.page.connectionSlug() ).toBe( 'local' );
		expect( d.page.group.canSave ).toBe( true );
		d.page.setTemplate( 2 );
		d.page.setRoot( 'pump1' );
		expect( d.page.rootHint() ).toContain( 'A NodeId' );
		expect( d.page.group.canSave ).toBe( false );
		d.page.setRoot( 'ns=2;i=5001' );
		d.page.setThresholds( {...histThresholdBlank, minTimeInterval: '1000'} );
		expect( d.page.group.canSave ).toBe( true );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['createHistGroup( slug:"pumps", name:"Pumps", serverConnectionId:1, histTemplateId:2, root:"ns=2;i=5001", minTimeInterval:1000 )'] );
	} );
	//an existing group:  its nodes with their template member and own columns, the template's members against them, and an edit of
	//the defaults sent as what changed
	it( 'lists the members and the resolution, and saves the thresholds changed', async ()=>{
		const d = await create( row );
		expect( d.page.isNew ).toBe( false );
		expect( d.page.excludedFields ).toBe( d.page.excludedFields );
		expect( d.admin.groupMembers ).toHaveBeenCalledWith( expect.anything(), 5 );
		expect( d.admin.templateMembers ).toHaveBeenCalledWith( expect.anything(), 2 );
		expect( d.page.members.rows() ).toHaveLength( 2 );
		expect( d.page.nodeLabel( d.page.members.rows()[0].node ) ).toBe( 'ns=2;i=6012' );
		expect( d.page.memberText( d.page.members.rows()[0] ) ).toBe( 'motorRpm' );
		expect( d.page.memberText( d.page.members.rows()[1] ) ).toBe( 'added by hand' );
		expect( d.page.cellText( d.page.members.rows()[0].thresholds, 'minTimeInterval' ) ).toBe( '1000' );
		//the server's word on each node (#379):  its name and the type served, with the path, description, rank and range behind them;  blank until read
		const [rpm, state] = d.page.members.rows();
		expect( d.page.nameText( rpm ) ).toBe( 'motorRpm' );
		expect( d.page.nameDetail( rpm ) ).toBe( 'pump1/motorRpm.  Served at Objects/local/pump1/motorRpm as ns=3;i=6012.  Measured motor speed, rpm.' );//its path on the source, where the endpoint serves it (#388), its description
		expect( d.page.typeText( rpm ) ).toBe( 'Double' );
		expect( d.page.typeDetail( rpm ) ).toBe( '{"nsu":"http://opcfoundation.org/UA/","i":11}.  Scalar.  EURange 0 to 3000' );
		expect( d.page.nameText( state ) ).toBe( 'unread' );
		expect( d.page.nameDetail( state ) ).toContain( 'Not yet read' );
		const unnamed = { ...rpm, attributes: {...rpm.attributes, displayName: ''} };//described, its DisplayName empty
		expect( d.page.nameText( unnamed ) ).toBe( 'no name' );
		expect( d.page.nameDetail( unnamed ) ).toBe( 'Its server gives it no DisplayName.  pump1/motorRpm.  Served at Objects/local/pump1/motorRpm as ns=3;i=6012.  Measured motor speed, rpm.' );
		expect( d.page.typeText( state ) ).toBe( '' );
		expect( d.page.typeDetail( state ) ).toBe( '' );
		expect( d.page.memberColumns.slice( 0, 4 ) ).toEqual( ['node', 'name', 'type', 'member'] );
		const rows = d.page.resolution();
		expect( rows.map( r=>[r.path, r.status] ) ).toEqual( [['motorRpm', 0], ['status', undefined]] );
		expect( d.page.unresolved() ).toBe( 1 );
		expect( d.page.statusText( rows[0] ) ).toBe( 'Resolved' );
		expect( d.page.statusText( rows[1] ) ).toContain( 'Not resolved' );
		d.page.setThresholds( {...d.page.group.thresholds, maxTimeInterval: '', exceptionDeviation: '2'} );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['updateHistGroup( id:5, exceptionDeviation:2, maxTimeInterval:null )'] );
	} );
	//Resolve:  every member answers, and one another group holds says which
	it( 'resolves and names the group that holds a node', async ()=>{
		const d = await create( row );
		await d.page.resolve();
		expect( d.admin.resolve ).toHaveBeenCalledWith( expect.anything(), 5 );
		const rows = d.page.resolution();
		expect( rows[1].status ).toBe( BadNodeIdExists );
		expect( d.page.statusText( rows[1] ) ).toContain( "group 'Other' holds the node" );
		expect( d.page.statusText( {...rows[1], holder: {id: 5, name: 'Pumps'}} ) ).toContain( 'this group holds the node already' );
		expect( d.page.statusText( {...rows[1], holder: {id: 9, name: ''}} ) ).toContain( 'group 9 holds the node' );//the gateway names no group the caller may not read
		expect( d.snackbar.info ).toHaveBeenCalledWith( '1 of 2 members resolved.' );
	} );
	//the rows say which members resolved:  a success the last resolve reported doesn't outlive its row, while a refusal's reason
	//stays until a node is removed or added, after which it may not hold
	it( 'keeps the resolution to the rows', async ()=>{
		const d = await create( row );
		await d.page.resolve();
		expect( d.page.resolution().map( r=>r.status ) ).toEqual( [0, BadNodeIdExists] );
		d.admin.groupMembers.mockResolvedValue( <any>d.page.members.rows().slice( 1 ) );//member 3's node removed, from another page say
		await d.page.refresh();
		expect( d.page.resolution().map( r=>r.status ) ).toEqual( [undefined, BadNodeIdExists] );
		expect( d.page.statusText( d.page.resolution()[0] ) ).toContain( 'Not resolved' );
		expect( d.page.unresolved() ).toBe( 2 );
		await d.page.removeNode( d.page.members.rows()[0] );
		expect( d.page.resolution().map( r=>r.status ) ).toEqual( [undefined, undefined] );
	} );
	//Add nodes:  each pick is its own create, from the group's root on its connection;  one refused doesn't stop the rest, and the
	//refusal - the gateway's, naming the holder - is said
	it( 'adds the nodes picked, and says which were refused', async ()=>{
		const d = await create( row );
		d.dialog.answers.set( HistNodePickerDialog, <HistPick[]>[
			{ node: new ExNodeId( {ns: 2, i: 6022, nsu: Pumps} ), path: [], name: 'rpm2' },
			{ node: new ExNodeId( {ns: 2, i: 6032, nsu: Pumps} ), path: [], name: 'rpm3' }
		] );
		const open = vi.spyOn( HistNodePickerDialog, 'open' );
		await d.page.addNodes();
		expect( open.mock.calls[0][1] ).toMatchObject( {mode: 'nodes', connection: 'local'} );
		expect( open.mock.calls[0][1].root?.nsu ).toBe( Pumps );
		expect( d.admin.createGroupMember ).toHaveBeenCalledTimes( 2 );
		expect( d.admin.createGroupMember.mock.calls[0][1] ).toBe( 5 );
		expect( d.snackbar.error ).toHaveBeenCalledWith( expect.stringContaining( "1 of 2 added.  rpm3:  " ) );
		expect( d.snackbar.error.mock.calls[0][0] ).toContain( "group 'Other' (9)" );
		expect( d.admin.groupMembers ).toHaveBeenCalledTimes( 2 );
		open.mockRestore();
	} );
	//the router reuses the page when another gateway's group opens, from Recently visited say:  the page then reads and writes the
	//gateway that row came from, and drops what the first gateway answers after - though the two rows share an id
	it( 'works on the gateway of the row it was given', async ()=>{
		const d = await create( row );
		let late!:( members:any[] )=>void;
		d.admin.groupMembers.mockImplementationOnce( ()=>new Promise<any[]>( r=>late = r ) );
		const refreshed = d.page.refresh();
		const other = { slug: 'h', mutate: async ()=>{} };
		d.data.next( {pageData: {row: {...row, slug: 'line1'}, routing: {}, schema: {enums: new Map()}, gateway: other}} );
		await flush( 3 );
		expect( d.page.gateway ).toBe( other );
		expect( d.admin.groupMembers ).toHaveBeenLastCalledWith( other, 5 );
		expect( d.admin.connections ).toHaveBeenLastCalledWith( other );
		late( [] );
		await refreshed;
		expect( d.page.members.rows() ).toHaveLength( 2 );
		await d.page.resolve();
		expect( d.admin.resolve ).toHaveBeenCalledWith( other, 5 );
	} );
	it( 'removes a node once confirmed, and not otherwise', async ()=>{
		const d = await create( row );
		d.dialog.answer = false;
		await d.page.removeNode( d.page.members.rows()[0] );
		expect( d.admin.purgeGroupMember ).not.toHaveBeenCalled();
		d.dialog.answer = true;
		await d.page.removeNode( d.page.members.rows()[0] );
		expect( d.admin.purgeGroupMember ).toHaveBeenCalledWith( expect.anything(), 7 );
	} );
	//#388:  Properties says where the gateway's own endpoint serves the group's members, and a member's name tooltip gives its path there
	//the Properties tab's body:  the group draws a body on a switch to its tab, as the member tooltips' test draws Members, so
	//Properties is left and come back to
	const served = async ( endpoint:unknown )=>{
		const d = await create( row, true, endpoint );
		for( const index of [1, 0] ){
			d.page.tabIndex.set( index );
			d.fixture.detectChanges( false );
			await flush( 3 );
			d.fixture.detectChanges( false );
		}
		return { ...d, text: d.fixture.nativeElement.querySelector( 'section[aria-label=Endpoint] .hint' )?.textContent };
	};
	//servers-7-web review #3, #4:  the paths are the endpoint's word, endpoint{ served(group) }, not built here
	it( 'says where the group is served', async ()=>{
		const d = await served( {url: 'opc.tcp://hub:4841', served: [servedRpm]} );
		expect( d.text ).toBe( 'opc.tcp://hub:4841 under Objects/local/pump1' );//the folder its members share
		expect( d.gateway.query ).toHaveBeenCalledWith( endpointServedQuery, {groupId: 5} );
		expect( d.page.nameDetail( d.page.members.rows()[0] ) ).toContain( 'Served at Objects/local/pump1/motorRpm as ns=3;i=6012' );
		expect( d.page.nameDetail( d.page.members.rows()[1] ) ).toContain( 'Not served by the endpoint' );//one it holds no node for
	} );
	it( 'gives the served path of a node with none on its source', async ()=>{
		const d = await served( {url: 'opc.tcp://hub:4841', served: [servedRpm, {id: 8, nodeId: 'ns=4;i=2259', path: 'Objects/local/i=2259'}]} );
		expect( d.text ).toBe( 'opc.tcp://hub:4841 under Objects/local' );
		expect( d.page.nameDetail( d.page.members.rows()[1] ) ).toBe( 'Not yet read from the server:  the collector reads a node once it has its item.  Served at Objects/local/i=2259 as ns=4;i=2259' );
	} );
	it( "and the connection's folder while it serves no member yet", async ()=>{
		const d = await served( {url: 'opc.tcp://hub:4841', served: []} );
		expect( d.text ).toBe( 'opc.tcp://hub:4841 under Objects/local' );
	} );
	it( 'and that it is not, on a gateway with no endpoint', async ()=>{
		const d = await served( null );
		expect( d.text ).toBe( 'Not served:  the gateway has no OPC UA endpoint.' );
		expect( d.page.nameDetail( d.page.members.rows()[0] ) ).not.toContain( 'Served' );
	} );
	it( 'or for a group the endpoint does not hold', async ()=>{
		const d = await served( {url: 'opc.tcp://hub:4841', served: null} );
		expect( d.text ).toContain( "Not served:  the endpoint on opc.tcp://hub:4841 doesn't hold the group" );
		expect( d.page.nameDetail( d.page.members.rows()[0] ) ).not.toContain( 'Served' );
	} );
	//the guid, never typed, is Properties' last line, not a field of the form
	it( 'shows the guid last', async ()=>{
		const d = await served( {url: 'opc.tcp://hub:4841', served: [servedRpm]} );
		const sections = [...d.fixture.nativeElement.querySelectorAll( 'section.form-section' )].map( ( s:Element )=>s.getAttribute( 'aria-label' ) );
		expect( sections ).toEqual( ['Source', 'Endpoint', 'Thresholds', 'Guid'] );
		expect( d.fixture.nativeElement.querySelector( 'section[aria-label=Guid] .value' ).textContent ).toBe( row.guid );
		expect( d.page.excludedFields ).toContain( 'guid' );
		d.page.copied( true );
		expect( d.snackbar.info ).toHaveBeenCalledWith( 'Guid copied.' );
	} );
	//the saved root by its browse path, as the gateway places it by the server's index, with its NodeId the hint
	it( 'shows the root by its path', async ()=>{
		const d = await create( row );
		expect( d.gateway.query ).toHaveBeenCalledWith( HistAdminService.rootPathsQuery, {opc: 'local', id: [{ns: 2, i: 5001}]} );
		expect( d.page.rootText() ).toBe( 'Objects/pumps/pump1' );//names only:  the NodeId is the hint
		expect( d.page.rootHint() ).toBe( 'ns=2;i=5001' );
	} );
	it( "or by its column's text where the gateway can't place it", async ()=>{
		const d = await create( row, false, undefined, {path: null} );
		expect( d.page.rootText() ).toBe( row.root );
		expect( d.page.rootHint() ).toBe( 'ns=2;i=5001' );
	} );
	it( 'or for a deleted group', async ()=>{
		const d = await create( {...row, deleted: '2026-10-25T00:00:00Z'}, false, {url: 'opc.tcp://hub:4841', served: null} );
		expect( d.page.servedText() ).toBe( 'Not served:  the group is deleted.' );
	} );
	//servers-7-web review #1:  the page reused for another gateway's group drops the last one's endpoint while the new one answers
	it( "drops the last gateway's endpoint on a new row", async ()=>{
		const d = await create( row );
		expect( d.page.endpoint()?.url ).toBe( 'opc.tcp://hub:4841' );
		d.gateway.query.mockImplementation( ()=>new Promise( ()=>{} ) );
		( d.page as any ).onRow();
		expect( d.page.endpoint() ).toBeUndefined();
	} );
	//reviews-issue/servers-0.2-node-columns.md #14:  each member's node, name and type, and each column's hint, is a tooltip on a span
	//a keyboard reaches - which the tooltip then describes to a screen reader (aria-describedby) - not on a cell, which nothing focuses
	it( 'puts the member tooltips on focusable elements', async ()=>{
		const d = await create( row, true );
		d.page.tabIndex.set( 1 );
		d.fixture.detectChanges( false );
		await flush( 3 );
		d.fixture.detectChanges( false );
		const tips = d.fixture.debugElement.queryAll( By.directive(MatTooltip) ).filter( e=>!!e.nativeElement.closest('mat-table') );
		expect( tips.length ).toBeGreaterThan( 4 );
		for( const tip of tips ){
			const host:HTMLElement = tip.nativeElement;
			expect( ['MAT-CELL', 'MAT-HEADER-CELL'] ).not.toContain( host.tagName );
			if( tip.injector.get( MatTooltip ).message )
				expect( host.tabIndex ).toBe( 0 );//a span of tabindex 0, or what is focusable already:  Remove's button.
		}
	} );
} );