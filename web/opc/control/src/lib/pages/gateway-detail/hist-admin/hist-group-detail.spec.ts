if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { ActivatedRoute, Router } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { vi } from 'vitest';
import { ComponentPageTitle } from 'jde-spa';
import { Mutation, SnackbarService } from 'jde-framework';
import { ExNodeId } from '../../../model/ex-node-id';
import { histThresholdBlank } from '../../../model/hist';
import { HistGroup, HistPick } from '../../../model/hist-admin';
import { HistAdminService } from '../../../services/hist-admin-service';
import { HistoryService } from '../../../services/history-service';
import { OpcStore } from '../../../services/opc-store';
import { HistGroupDetail } from './hist-group-detail';
import { HistNodePickerDialog } from './hist-node-picker-dialog';

const settle = async ()=>{ for( let i=0; i<3; ++i ) await new Promise( r=>setTimeout( r ) ); };
const Pumps = 'urn:jde:pumps';
const BadNodeIdExists = 0x805E0000;
const server = { namespaces: [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: Pumps}] };
const row = { id: 5, name: 'Pumps', slug: 'pumps', guid: '0f8fad5b-d9cb-469f-a165-70867728950e', serverConnection: {id: 1, name: 'Local'}, histTemplate: {id: 2, name: 'Pump'}, root: '{"nsu":"urn:jde:pumps","i":5001}', maxTimeInterval: 60000 };
//The page over stubs:  the gateway records the mutations Save sends, the admin service what the tabs ask and send, and the dialog
//answers a confirmation or the picker's picks.
const create = async ( pageRow:any )=>{
	const mutations:string[] = [];
	const gateway = { slug: 'g', mutate: async ( m:Mutation[]|string )=>{ mutations.push( ...(Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m]) ); } };
	const snackbar = { exception: vi.fn(), error: vi.fn(), info: vi.fn() };
	const admin = {
		connections: vi.fn( async ()=>[{id: 1, slug: 'local', name: 'Local'}, {id: 3, slug: 'plc', name: 'PLC'}] ),
		templates: vi.fn( async ()=>[{id: 2, name: 'Pump'}] ),
		groupMembers: vi.fn( async ()=>[
			{ id: 7, node: new ExNodeId( {i: 6012, nsu: Pumps} ), templateNodeId: 3, thresholds: {...histThresholdBlank, minTimeInterval: '1000'} },
			{ id: 8, node: new ExNodeId( {i: 2259, nsu: 'http://opcfoundation.org/UA/'} ), templateNodeId: undefined, thresholds: {...histThresholdBlank} }
		] ),
		templateMembers: vi.fn( async ()=>[{id: 3, templateId: 2, path: [{nsu: Pumps, name: 'motorRpm'}], thresholds: histThresholdBlank}, {id: 4, templateId: 2, path: [{nsu: Pumps, name: 'status'}], thresholds: histThresholdBlank}] ),
		createGroupMember: vi.fn( async ( _g:unknown, _id:number, node:ExNodeId )=>{ if( node.id==6032 ) throw new Error( "'{\"nsu\":\"urn:jde:pumps\",\"i\":6032}' is already in group 'Other' (9) on this connection:  a node belongs to one group per server." ); return 10; } ),
		purgeGroupMember: vi.fn( async ()=>{} ),
		resolve: vi.fn( async ()=>[{templateNode: 3, path: [{nsu: Pumps, name: 'motorRpm'}], node: new ExNodeId( {i: 6012, nsu: Pumps} ), status: 0}, {templateNode: 4, path: [{nsu: Pumps, name: 'status'}], status: BadNodeIdExists, holder: {id: 9, name: 'Other'}}] )
	};
	const dialog = { confirmed: true, picks: <HistPick[]|undefined>undefined, open: ( cmp:unknown )=>({ afterClosed: ()=>({ subscribe: ( f:( y:unknown )=>void )=>f( cmp===HistNodePickerDialog ? dialog.picks : dialog.confirmed ) }) }) };
	const data = new BehaviorSubject<any>( {pageData: {row: pageRow, routing: {}, schema: {enums: new Map()}, gateway}} );//the route's data, the gateway with the row:  next() opens another row on the page, as the router reusing it does
	TestBed.configureTestingModule({ providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the zoneless fixture's own first change detection would run ngOnInit a second time, onto the row #load nulled
		{ provide: ActivatedRoute, useValue: {data} },
		{ provide: Router, useValue: {navigate: vi.fn()} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: snackbar },
		{ provide: HistAdminService, useValue: admin },
		{ provide: HistoryService, useValue: {} },
		{ provide: OpcStore, useValue: {getConnection: async ()=>server} },
		{ provide: MatDialog, useValue: dialog }
	]});
	const page = TestBed.createComponent( HistGroupDetail ).componentInstance;
	page.ngOnInit();
	await settle();
	return { page, mutations, admin, snackbar, dialog, data };
};

describe( 'HistGroupDetail', ()=>{
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
		expect( d.mutations ).toEqual( ['createHistGroup( slug:"pumps", name:"Pumps", serverConnectionId:1, histTemplateId:2, root:"ns=2;i=5001", minTimeInterval:1000 )'] );
	} );
	//an existing group:  its nodes with their template member and own columns, the template's members against them, and an edit of
	//the defaults sent as what changed
	it( 'lists the members and the resolution, and saves the thresholds changed', async ()=>{
		const d = await create( row );
		expect( d.page.isNew ).toBe( false );
		expect( d.page.excludedFields ).toBe( d.page.excludedFields );
		expect( d.page.excludedFields ).not.toContain( 'guid' );
		expect( d.admin.groupMembers ).toHaveBeenCalledWith( expect.anything(), 5 );
		expect( d.admin.templateMembers ).toHaveBeenCalledWith( expect.anything(), 2 );
		expect( d.page.members() ).toHaveLength( 2 );
		expect( d.page.nodeLabel( d.page.members()[0].node ) ).toBe( 'ns=2;i=6012' );
		expect( d.page.memberText( d.page.members()[0] ) ).toBe( 'motorRpm' );
		expect( d.page.memberText( d.page.members()[1] ) ).toBe( 'added by hand' );
		expect( d.page.cellText( d.page.members()[0].thresholds, 'minTimeInterval' ) ).toBe( '1000' );
		const rows = d.page.resolution();
		expect( rows.map( r=>[r.path, r.status] ) ).toEqual( [['motorRpm', 0], ['status', undefined]] );
		expect( d.page.unresolved() ).toBe( 1 );
		expect( d.page.statusText( rows[0] ) ).toBe( 'Resolved' );
		expect( d.page.statusText( rows[1] ) ).toContain( 'Not resolved' );
		d.page.setThresholds( {...d.page.group.thresholds, maxTimeInterval: '', exceptionDeviation: '2'} );
		await d.page.onSubmitClick();
		expect( d.mutations ).toEqual( ['updateHistGroup( id:5, exceptionDeviation:2, maxTimeInterval:null )'] );
	} );
	//Resolve:  every member answers, and one another group holds says which
	it( 'resolves and names the group that holds a node', async ()=>{
		const d = await create( row );
		await d.page.resolve();
		expect( d.admin.resolve ).toHaveBeenCalledWith( expect.anything(), 5, expect.any( Function ) );
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
		d.admin.groupMembers.mockResolvedValue( <any>d.page.members().slice( 1 ) );//member 3's node removed, from another page say
		await d.page.refresh();
		expect( d.page.resolution().map( r=>r.status ) ).toEqual( [undefined, BadNodeIdExists] );
		expect( d.page.statusText( d.page.resolution()[0] ) ).toContain( 'Not resolved' );
		expect( d.page.unresolved() ).toBe( 2 );
		await d.page.removeNode( d.page.members()[0] );
		expect( d.page.resolution().map( r=>r.status ) ).toEqual( [undefined, undefined] );
	} );
	//Add nodes:  each pick is its own create, from the group's root on its connection;  one refused doesn't stop the rest, and the
	//refusal - the gateway's, naming the holder - is said
	it( 'adds the nodes picked, and says which were refused', async ()=>{
		const d = await create( row );
		d.dialog.picks = [
			{ node: new ExNodeId( {ns: 2, i: 6022, nsu: Pumps} ), path: [], name: 'rpm2' },
			{ node: new ExNodeId( {ns: 2, i: 6032, nsu: Pumps} ), path: [], name: 'rpm3' }
		];
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
		await settle();
		expect( d.page.gateway ).toBe( other );
		expect( d.admin.groupMembers ).toHaveBeenLastCalledWith( other, 5 );
		expect( d.admin.connections ).toHaveBeenLastCalledWith( other );
		late( [] );
		await refreshed;
		expect( d.page.members() ).toHaveLength( 2 );
		await d.page.resolve();
		expect( d.admin.resolve ).toHaveBeenCalledWith( other, 5, expect.any( Function ) );
	} );
	it( 'removes a node once confirmed, and not otherwise', async ()=>{
		const d = await create( row );
		d.dialog.confirmed = false;
		await d.page.removeNode( d.page.members()[0] );
		expect( d.admin.purgeGroupMember ).not.toHaveBeenCalled();
		d.dialog.confirmed = true;
		await d.page.removeNode( d.page.members()[0] );
		expect( d.admin.purgeGroupMember ).toHaveBeenCalledWith( expect.anything(), 7, expect.any( Function ) );
	} );
} );