import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { By } from '@angular/platform-browser';
import { SnackbarService } from 'jde-framework';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';
import { Flow, FlowDeployed, FlowGraph } from '../../../../model/flow';
import { normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowService } from '../../../../services/flow-service';
import { FlowCanvas } from '../canvas/flow-canvas';//here ahead of the editor's own import of it, which then waits for nothing
import { FlowDeployDialog } from './flow-deploy-dialog';
import { FlowEditor, FlowEditorSource } from './flow-editor';

const node = ( id:string, type:string, more:object={} )=>({ id, type, name: '', x: 0, y: 0, props: {}, wires: <string[][]>[], ...more });
const deployed = ():FlowGraph=>normalizeFlowGraph( {nodes: [node( 'in', 'inject', {wires: [['fn']]} ), node( 'fn', 'function', {name: 'Scale', x: 220, props: {code: 'return msg;'}, wires: [['out']]} ), node( 'out', 'debug', {x: 440} )]} );
const row = ( more:object={} )=>new Flow( {id: 7, name: 'Pump', slug: 'pump', guid: 'g', enabled: true, version: 2, ...more} );

const create = async ( {flow=row(), source={deployed: deployed()}}:{flow?:Flow; source?:FlowEditorSource}={} )=>{
	const answer:FlowDeployed = { version: 3, userName: 'ann', running: true, dropped: 0 };
	const flows = { deploy: vi.fn( async ( ..._args:unknown[] )=>answer ), setEnabled: vi.fn( async ( ..._args:unknown[] )=>{} ), inject: vi.fn( async ( ..._args:unknown[] )=>{} ) };
	const dialog = dialogStub( 'the comment' ), snackbar = snackbarStub(), gateway = { postQL: vi.fn() };
	TestBed.configureTestingModule( {providers: [{ provide: FlowService, useValue: flows }, { provide: MatDialog, useValue: dialog }, { provide: SnackbarService, useValue: snackbar }]} );
	const fixture = TestBed.createComponent( FlowEditor ), editor = fixture.componentInstance;
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'flow', flow );
	fixture.componentRef.setInput( 'source', source );
	const deploys:FlowDeployed[] = [], enabled:boolean[] = [];
	editor.deployed.subscribe( d=>deploys.push( d ) );
	editor.enabledChange.subscribe( e=>enabled.push( e ) );
	for( let i=0; i<100 && !editor.ready(); ++i )//the canvas is imported once the editor is shown
		await stable( fixture, 1 );
	expect( fixture.debugElement.query( By.directive( FlowCanvas ) ) ).not.toBeNull();
	await stable( fixture );
	const element = <HTMLElement>fixture.nativeElement;
	const asked = ()=><{version:number; enabled:boolean; fault?:string}>dialog.open.mock.lastCall![1]!.data;
	return { fixture, editor, element, flows, dialog, snackbar, gateway, deploys, enabled, asked };
};

describe( 'FlowEditor', ()=>{
	beforeEach( ()=>vi.stubGlobal( 'ResizeObserver', class{ observe(){} unobserve(){} disconnect(){} } ) );
	afterEach( ()=>vi.unstubAllGlobals() );

	it( 'opens the version deployed, with nothing to deploy', async ()=>{
		const { editor, element } = await create();
		expect( element.querySelectorAll( 'flow-node-view' ).length ).toBe( 3 );
		expect( editor.graph() ).toEqual( deployed() );
		expect( [editor.dirty(), editor.canDeploy()] ).toEqual( [false, false] );
		expect( element.querySelector( '.state' )!.textContent!.replace( /\s+/g, ' ' ).trim() ).toBe( 'Version 2, enabled' );
		expect( element.querySelector( 'flow-node-form' ) ).toBeNull();
	} );
	//an import's graph, or a version opened from Versions:  on the canvas, over the one deployed, for Deploy to send
	it( 'opens a draft over the version deployed', async ()=>{
		const draft = { nodes: [node( 'in', 'inject', {wires: [[]]} )] };
		const { editor, element, fixture } = await create( {source: {deployed: deployed(), draft}} );
		expect( editor.graph() ).toEqual( draft );
		expect( [editor.dirty(), editor.canDeploy()] ).toEqual( [true, true] );
		expect( element.querySelector( '.dirty' )!.textContent ).toContain( 'changes not deployed' );
		editor.revert();
		await stable( fixture );
		expect( [editor.graph(), editor.dirty()] ).toEqual( [deployed(), false] );
		fixture.componentRef.setInput( 'source', {deployed: {nodes: []}} );//another flow, with nothing deployed
		fixture.componentRef.setInput( 'flow', row( {id: 8, version: undefined} ) );
		await stable( fixture );
		expect( [editor.graph().nodes.length, editor.dirty(), editor.canDeploy()] ).toEqual( [0, false, true] );//a first deploy, of whatever is drawn
	} );
	it( 'shows the selected node its form, whose edits go to the canvas', async ()=>{
		const { editor, element, fixture } = await create();
		editor.show( 'fn' );
		await stable( fixture );
		expect( editor.selectedNode()?.id ).toBe( 'fn' );
		expect( element.querySelector( 'flow-node-form header h3' )!.textContent ).toBe( 'function' );
		editor.patch( 'fn', {props: {code: 'return msg;', outputs: 150}} );
		await stable( fixture );
		expect( editor.dirty() ).toBe( true );
		expect( [...editor.faults()] ).toEqual( [['fn', '`outputs` must be a whole number, 100 at most.']] );
		expect( element.querySelector( 'flow-node-form .fault' )!.textContent ).toContain( '100 at most' );
		expect( element.querySelector( '[data-f-node-id="fn"]' )!.classList.contains( 'faulty' ) ).toBe( true );
	} );
	it( 'deploys the canvas with the comment given', async ()=>{
		const { editor, flows, dialog, deploys, snackbar, gateway, asked, fixture } = await create();
		editor.patch( 'fn', {name: 'Scale up'} );
		await stable( fixture );
		await editor.deploy();
		expect( dialog.open.mock.lastCall![0] ).toBe( FlowDeployDialog );
		expect( asked() ).toMatchObject( {name: 'Pump', version: 3, enabled: true, fault: undefined} );
		expect( flows.deploy ).toHaveBeenCalledWith( gateway, 7, editor.graph(), 'the comment' );
		expect( editor.graph().nodes[1].name ).toBe( 'Scale up' );
		expect( [editor.dirty(), editor.refusal()] ).toEqual( [false, undefined] );
		expect( deploys ).toEqual( [{version: 3, userName: 'ann', running: true, dropped: 0}] );
		expect( snackbar.info ).toHaveBeenCalledWith( 'Version 3 deployed, and running.' );
	} );
	it( 'sends nothing where the dialog is cancelled', async ()=>{
		const { editor, flows, dialog, fixture } = await create();
		editor.patch( 'fn', {name: 'x'} );
		await stable( fixture );
		dialog.answer = undefined;
		await editor.deploy();
		expect( flows.deploy ).not.toHaveBeenCalled();
		expect( editor.dirty() ).toBe( true );
	} );
	//the editor's own rules say a fault ahead of the deploy, and the gateway's answer is the one that counts:  its refusal
	//names the node, which the canvas marks and selects
	it( 'marks the node a refusal names', async ()=>{
		const { editor, flows, element, asked, fixture, deploys } = await create();
		editor.patch( 'fn', {props: {}} );
		await stable( fixture );
		flows.deploy.mockRejectedValue( new Error( "(400)Node 'fn':  `code` must be text, the body." ) );
		await editor.deploy();
		await stable( fixture );
		expect( asked().fault ).toBe( "Node 'Scale':  `code` must be text, the body." );//what the editor found, said in the dialog
		expect( editor.refusal() ).toEqual( {node: 'fn', message: '`code` must be text, the body.'} );
		expect( element.querySelector( '.refusal' )!.textContent ).toContain( "Node 'Scale':  `code` must be text, the body." );
		expect( element.querySelector( '[data-f-node-id="fn"]' )!.classList.contains( 'refused' ) ).toBe( true );
		expect( editor.selected() ).toBe( 'fn' );
		expect( [editor.dirty(), deploys] ).toEqual( [true, []] );//nothing was written
		flows.deploy.mockRejectedValue( new Error( '(403)The deployer may not update the flow.' ) );
		await editor.deploy();
		expect( editor.refusal() ).toEqual( {message: 'The deployer may not update the flow.'} );
	} );
	it( 'says why a version deployed did not start', async ()=>{
		const { editor, flows, snackbar, fixture } = await create();
		editor.patch( 'fn', {name: 'x'} );
		await stable( fixture );
		flows.deploy.mockResolvedValue( {version: 3, userName: 'ann', running: false, dropped: 2, fault: {node: 'out', message: 'The flow server is not running.'}} );
		await editor.deploy();
		expect( editor.refusal() ).toEqual( {node: 'out', message: 'The flow server is not running.'} );
		expect( editor.dirty() ).toBe( false );//the version is written
		expect( snackbar.info ).toHaveBeenCalledWith( 'Version 3 deployed, and did not start.  2 messages of the run before dropped.' );
	} );
	it( 'enables and disables the flow, and injects through the nodes of the version running', async ()=>{
		const { editor, flows, enabled, gateway, element, snackbar } = await create();
		await editor.setEnabled( false );
		expect( flows.setEnabled ).toHaveBeenCalledWith( gateway, 7, false );
		expect( enabled ).toEqual( [false] );
		expect( [...editor.injectable()] ).toEqual( ['in'] );
		(<HTMLButtonElement>element.querySelector( '[data-f-node-id="in"] .inject' )).click();
		expect( flows.inject ).toHaveBeenCalledWith( gateway, 7, 'in' );
		flows.setEnabled.mockRejectedValue( new Error( 'no' ) );
		await editor.setEnabled( true );
		expect( enabled ).toEqual( [false] );
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not enable the flow.', expect.anything() );
	} );
	it( 'offers no inject for a flow that is disabled, or not deployed', async ()=>{
		const disabled = await create( {flow: row( {enabled: false} )} );
		expect( [...disabled.editor.injectable()] ).toEqual( [] );
		TestBed.resetTestingModule();//a second editor, on a flow with no version
		const fresh = await create( {flow: row( {version: undefined} ), source: {deployed: {nodes: []}, draft: deployed()}} );
		expect( [...fresh.editor.injectable()] ).toEqual( [] );
	} );
} );