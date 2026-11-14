import { Component, forwardRef, input, output } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { By } from '@angular/platform-browser';
import { Mutation, Properties } from 'jde-framework';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';
import { flush, stable } from 'jde-spa/testing';
import { Flow, FlowGraph, FlowRun, FlowVersion } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { HistGroupAccess } from '../hist-admin/hist-group-access';
import { FlowActivity } from './activity/flow-activity';
import { FlowEditor } from './editor/flow-editor';
import { FlowContext } from './flow-context';
import { flowAccessSubject, FlowDetail } from './flow-detail';
import { FlowDrafts } from './flow-drafts';
import { FlowVersions } from './flow-versions';
import { FlowHistory } from './history/flow-history';

//the tabs' components, standing in for the real ones:  the page is what gives each its inputs.  The editor's is the page's query
//of the editor, which asks it whether the canvas may be replaced.
@Component( {selector: 'flow-editor', template: '', providers: [{ provide: FlowEditor, useExisting: forwardRef( ()=>EditorStub ) }]} )
class EditorStub{ gateway = input<unknown>(); flow = input<unknown>(); source = input<unknown>(); deployed = output<unknown>(); enabledChange = output<boolean>(); mayReplace = vi.fn( async ( _title:string )=>true ); }
@Component( {selector: 'flow-activity', template: ''} )
class ActivityStub{ gateway = input<unknown>(); flow = input<unknown>(); versions = input<unknown>(); active = input<boolean>(); refresh = output<void>(); }
@Component( {selector: 'flow-history', template: ''} )
class HistoryStub{ gateway = input<unknown>(); flow = input<unknown>(); versions = input<unknown>(); request = input<unknown>(); }
@Component( {selector: 'flow-versions', template: ''} )
class VersionsStub{ gateway = input<unknown>(); flow = input<unknown>(); versions = input<unknown>(); loading = input<boolean>(); open = output<unknown>(); openRun = output<FlowRun>(); refresh = output<void>(); }
@Component( {selector: 'flow-context', template: ''} )
class ContextStub{ gateway = input<unknown>(); flow = input<unknown>(); nodes = input<unknown>(); }
@Component( {selector: 'hist-group-access', template: ''} )
class AccessStub{ group = input<unknown>(); subject = input<unknown>(); }

//a tab switched to:  its content is made as the bodies' slide starts and ends, which jsdom never says, so each is told it did
const shown = async ( fixture:{ detectChanges():void; whenStable():Promise<unknown>; nativeElement:HTMLElement } )=>{
	await stable( fixture );
	for( const type of ['transitionstart', 'transitionend'] )
		fixture.nativeElement.querySelectorAll( '.mat-mdc-tab-body-content' ).forEach( e=>e.dispatchEvent( new Event( type, {bubbles: true} ) ) );
	await stable( fixture );
};
const graph = ( ...ids:string[] ):FlowGraph=>({ nodes: ids.map( id=>({id, type: 'debug', name: '', x: 0, y: 0, props: {}, wires: []}) ) });
const version = ( n:number, g:FlowGraph ):FlowVersion=>({ id: n, version: n, graph: g, identityId: 1, userName: 'ann' });
const row = { id: 7, name: 'Pump', slug: 'pump', guid: 'g-7', enabled: true, version: 2, trace: 'counts' };

const create = async ( pageRow:object=row, {versions=[version( 2, graph( 'a', 'b' ) ), version( 1, graph( 'a' ) )], draft}:{versions?:FlowVersion[]|Error; draft?:FlowGraph}={} )=>{
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{} ) };
	const flows = { versions: vi.fn( async ( ..._args:unknown[] )=>{ if( versions instanceof Error ) throw versions; return versions; } ) };
	const dialog = dialogStub( true );//a delete's confirmation
	const {data, snackbar, router} = configureDetailPage( pageRow, {gateway, providers: [{ provide: FlowService, useValue: flows }, { provide: MatDialog, useValue: dialog }]} );
	TestBed.overrideComponent( Properties, {set: {template: '', imports: []}} );//the form reads a whole schema
	TestBed.overrideComponent( FlowDetail, {remove: {imports: [FlowEditor, FlowActivity, FlowHistory, FlowVersions, FlowContext, HistGroupAccess]}, add: {imports: [EditorStub, ActivityStub, HistoryStub, VersionsStub, ContextStub, AccessStub]}} );
	if( draft )
		TestBed.inject( FlowDrafts ).put( (<{guid:string}>pageRow).guid, draft );
	const fixture = TestBed.createComponent( FlowDetail ), page = fixture.componentInstance;
	fixture.detectChanges();//its first change detection runs ngOnInit
	await flush( 3 );
	const mutations = ()=>gateway.mutate.mock.calls.flatMap( ( [m] )=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	return { page, fixture, gateway, flows, snackbar, router, data, dialog, mutations };
};

describe( 'FlowDetail', ()=>{
	//the editor opens with the current version's graph, once the versions are read
	it( "opens the editor on the current version's graph", async ()=>{
		const d = await create();
		expect( d.flows.versions ).toHaveBeenCalledWith( d.gateway, 7 );
		expect( d.page.source() ).toEqual( {deployed: graph( 'a', 'b' ), draft: undefined} );
		expect( d.page.nodes().map( n=>n.id ) ).toEqual( ['a', 'b'] );
		expect( d.page.versions().map( v=>v.version ) ).toEqual( [2, 1] );
	} );
	it( 'opens a flow with nothing deployed on an empty canvas, on the Editor tab', async ()=>{
		const d = await create( {...row, version: null}, {versions: []} );
		d.page.tabIndex.set( 3 );
		d.data.next( {pageData: {row: {...row, id: 8, guid: 'g-8', version: null}, routing: {}, schema: {enums: new Map()}, gateway: d.gateway}} );
		await flush( 3 );
		expect( d.page.source() ).toEqual( {deployed: {nodes: []}, draft: undefined} );
		expect( d.page.tabIndex() ).toBe( 0 );
	} );
	//an import's graph, handed over by the flow's guid, is the editor's draft:  taken once
	it( "opens an import's graph as the editor's draft", async ()=>{
		const d = await create( {...row, version: null}, {versions: [], draft: graph( 'x' )} );
		expect( d.page.source() ).toEqual( {deployed: {nodes: []}, draft: graph( 'x' )} );
		expect( TestBed.inject( FlowDrafts ).take( 'g-7' ) ).toBeUndefined();
	} );
	//an empty canvas would deploy over a version it never showed
	it( 'opens no editor where the versions could not be read', async ()=>{
		const d = await create( row, {versions: new Error( 'the gateway is down' )} );
		expect( d.page.source() ).toBeUndefined();
		expect( d.page.versionsError() ).toContain( 'the gateway is down' );
		d.flows.versions.mockResolvedValue( [version( 2, graph( 'a' ) )] );
		d.page.retry();
		await flush( 3 );
		expect( d.page.source() ).toEqual( {deployed: graph( 'a' ), draft: undefined} );
		expect( d.page.versionsError() ).toBeUndefined();
	} );
	//the draft is the page's until the editor has it (reviews-issue/455-flows-web-editor.md #2)
	it( "keeps an import's draft for a Retry", async ()=>{
		const d = await create( {...row, version: null}, {versions: new Error( 'the gateway is down' ), draft: graph( 'x' )} );
		expect( d.page.source() ).toBeUndefined();
		d.flows.versions.mockResolvedValue( [] );
		d.page.retry();
		await flush( 3 );
		expect( d.page.source() ).toEqual( {deployed: {nodes: []}, draft: graph( 'x' )} );
	} );
	//a Refresh answered after the deploy's read is not the versions (reviews-issue/455-flows-web-editor.md #7)
	it( 'takes the newest read of the versions, whatever order the answers come in', async ()=>{
		const d = await create();
		let late!:( v:FlowVersion[] )=>void;
		d.flows.versions.mockImplementationOnce( ()=>new Promise<FlowVersion[]>( resolve=>late = resolve ) );
		d.page.refreshVersions();
		d.flows.versions.mockResolvedValueOnce( [version( 3, graph( 'a', 'b', 'c' ) ), version( 2, graph( 'a', 'b' ) )] );
		d.page.onDeployed( {version: 3, userName: 'ann', running: true, dropped: 0} );
		await flush( 3 );
		expect( [d.page.versions().map( v=>v.version ), d.page.versionsLoading()] ).toEqual( [[3, 2], false] );
		late( [version( 2, graph( 'a', 'b' ) ), version( 1, graph( 'a' ) )] );
		await flush( 3 );
		expect( [d.page.versions().map( v=>v.version ), d.page.versionsLoading()] ).toEqual( [[3, 2], false] );
		expect( d.page.nodes().map( n=>n.id ) ).toEqual( ['a', 'b', 'c'] );
	} );
	it( 'takes a deploy and an enable onto the row, and reads the versions again', async ()=>{
		const d = await create();
		const source = d.page.source();
		d.page.properties.set( new Flow( {...d.page.properties(), name: 'Pump 1'} ) );//typed, not saved
		d.flows.versions.mockResolvedValue( [version( 3, graph( 'a', 'b', 'c' ) ), version( 2, graph( 'a', 'b' ) )] );
		d.page.onDeployed( {version: 3, userName: 'ann', running: true, dropped: 0} );
		d.page.onEnabled( false );
		await flush( 3 );
		expect( [d.page.row.version, d.page.row.enabled, d.page.row.name] ).toEqual( [3, false, 'Pump'] );
		expect( d.page.properties().name ).toBe( 'Pump 1' );//the form keeps what is typed in it
		expect( d.page.versions().map( v=>v.version ) ).toEqual( [3, 2] );
		expect( d.page.source() ).toBe( source );//the canvas is not loaded again:  it holds what was deployed
		expect( d.page.nodes().map( n=>n.id ) ).toEqual( ['a', 'b', 'c'] );
	} );
	it( "puts a version's graph back on the canvas", async ()=>{
		const d = await create();
		d.page.tabIndex.set( 1 );
		await d.page.openVersion( d.page.versions()[1] );
		expect( d.page.source() ).toEqual( {deployed: graph( 'a', 'b' ), draft: graph( 'a' )} );
		expect( d.page.tabIndex() ).toBe( 0 );
		expect( d.snackbar.info ).toHaveBeenCalledWith( 'Version 1 is on the canvas, not deployed:  Deploy makes it the next version.' );
		await d.page.openVersion( d.page.versions()[0] );//the current one:  no draft
		expect( d.page.source() ).toEqual( {deployed: graph( 'a', 'b' ), draft: undefined} );
	} );
	//a load begins the canvas's history again, so the editor asks first where it holds what isn't deployed (reviews-issue/455-flows-web-editor.md #3)
	it( 'opens a version only once the editor lets what it holds go', async ()=>{
		const d = await create();
		await stable( d.fixture );
		const editor = <EditorStub>d.fixture.debugElement.query( By.directive( EditorStub ) ).componentInstance;
		const source = d.page.source();
		editor.mayReplace.mockResolvedValue( false );
		await d.page.openVersion( d.page.versions()[1] );
		expect( editor.mayReplace ).toHaveBeenCalledWith( 'Open version 1 on the canvas?' );
		expect( d.page.source() ).toBe( source );
		editor.mayReplace.mockResolvedValue( true );
		await d.page.openVersion( d.page.versions()[1] );
		expect( d.page.source() ).toEqual( {deployed: graph( 'a', 'b' ), draft: graph( 'a' )} );
	} );
	//the Activity tab is live while it is the one shown;  a replay opened from Versions goes to History with it (#456)
	it( 'subscribes the Activity tab while shown, and opens a replay in History', async ()=>{
		const d = await create();
		await stable( d.fixture );
		expect( d.fixture.debugElement.query( By.directive( ActivityStub ) ) ).toBeNull();//a tab's content is made once it is shown
		d.page.tabIndex.set( 1 );
		await shown( d.fixture );
		const activity = <ActivityStub>d.fixture.debugElement.query( By.directive( ActivityStub ) ).componentInstance;
		expect( activity.active() ).toBe( true );
		const run:FlowRun = { id: 5, version: 2, mode: 'dry', userName: 'ann', status: 'completed' };
		d.page.openRun( run );
		await shown( d.fixture );
		expect( [d.page.tabIndex(), activity.active()] ).toEqual( [FlowDetail.historyTab, false] );
		const history = <HistoryStub>d.fixture.debugElement.query( By.directive( HistoryStub ) ).componentInstance;
		expect( history.request() ).toEqual( {run, n: 1} );
		d.page.openRun( run );
		expect( d.page.historyRequest() ).toEqual( {run, n: 2} );//the same run asked twice is taken twice
	} );
	//the row's own form:  its name, description and trace level.  A save, a cancel and a delete go to the list, `<page>/flows`,
	//which redirects to the gateway's page
	it( "saves the row's form and goes back to the list", async ()=>{
		const d = await create();
		d.page.properties.set( new Flow( {...d.page.properties(), name: 'Pump 1', trace: null} ) );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['updateFlow( id:7, name:"Pump 1", trace:null )'] );
		expect( d.router.navigate.mock.lastCall![0] ).toEqual( ['..'] );
		d.page.onCancelClick();
		expect( d.router.navigate.mock.lastCall![0] ).toEqual( ['..'] );
		await d.page.onDeleteClick();
		expect( (<{message:string}>d.dialog.open.mock.lastCall![1]!.data).message ).toContain( 'The flow stops.' );
		expect( d.mutations().at( -1 ) ).toBe( 'deleteFlow(id:7)' );
	} );
	it( "grants on the flow's own resource, as a group's tab does", ()=>{
		expect( flowAccessSubject.key ).toEqual( {schemaName: 'gateway', slug: 'flows'} );
		expect( Object.values( flowAccessSubject.rights ) ).toEqual( ['None', 'Read', 'Update', 'Delete', 'Purge', 'Administer'] );
		expect( flowAccessSubject.held( 'Pump' ) ).toBe( 'read Pump, change it, deploy it or delete it' );
	} );
} );