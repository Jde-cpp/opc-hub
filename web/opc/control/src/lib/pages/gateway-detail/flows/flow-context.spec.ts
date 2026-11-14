import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { SnackbarService } from 'jde-framework';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';
import { Flow, FlowContextValue, FlowNode } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { contextValueProblem, FlowContext } from './flow-context';

const rows:FlowContextValue[] = [
	{ id: 1, flowId: 7, nodeId: 'fn', name: 'count', value: '2' },
	{ id: 2, flowId: 7, nodeId: '', name: 'limits', value: '{"high":9}' },
	{ id: 3, flowId: undefined, nodeId: '', name: 'site', value: '"A"' },
	{ id: 4, flowId: 7, nodeId: 'gone', name: 'mark', value: '1' }
];
const nodes:FlowNode[] = [{id: 'fn', type: 'function', name: 'Scale', x: 0, y: 0, props: {}, wires: []}];

const create = async ( confirmed=true )=>{
	const flows = { contexts: vi.fn( async ( ..._args:unknown[] )=>rows ), setContext: vi.fn( async ( ..._args:unknown[] )=>{} ) };
	const dialog = dialogStub( confirmed ), snackbar = snackbarStub(), gateway = { query: vi.fn(), postQL: vi.fn() };
	TestBed.configureTestingModule( {providers: [{ provide: FlowService, useValue: flows }, { provide: MatDialog, useValue: dialog }, { provide: SnackbarService, useValue: snackbar }]} );
	const fixture = TestBed.createComponent( FlowContext ), tab = fixture.componentInstance;
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'flow', new Flow( {id: 7, name: 'Pump', slug: 'pump'} ) );
	fixture.componentRef.setInput( 'nodes', nodes );
	await stable( fixture );
	return { fixture, tab, element: <HTMLElement>fixture.nativeElement, flows, dialog, snackbar, gateway };
};

describe( 'FlowContext', ()=>{
	it( 'lists the values at their scopes', async ()=>{
		const { tab, flows, gateway, element } = await create();
		expect( flows.contexts ).toHaveBeenCalledWith( gateway, 7 );
		expect( tab.rows().map( r=>tab.scopeText( r ) ) ).toEqual( ['Scale', 'Flow', 'Gateway', 'gone'] );//a node by its name, else its id
		expect( tab.scopeDetail( rows[2] ) ).toBe( "The gateway's, which every flow shares" );
		expect( element.querySelectorAll( 'mat-row' ).length ).toBe( 4 );
	} );
	//setFlowContext takes the value as json, at the scope the row has:  no flow is the gateway's, no node the flow's
	it( 'edits a value at its scope', async ()=>{
		const { tab, flows, gateway } = await create();
		tab.edit( rows[0] );
		tab.set( 'text', '3' );
		await tab.save();
		expect( flows.setContext ).toHaveBeenLastCalledWith( gateway, {flowId: 7, nodeId: 'fn', name: 'count'}, 3 );
		expect( tab.editing() ).toBeUndefined();
		expect( flows.contexts ).toHaveBeenCalledTimes( 2 );
		tab.edit( rows[2] );
		tab.set( 'text', '{"name":"B"}' );
		await tab.save();
		expect( flows.setContext ).toHaveBeenLastCalledWith( gateway, {flowId: undefined, nodeId: undefined, name: 'site'}, {name: 'B'} );
	} );
	it( 'adds a value once it has a name, a node and json', async ()=>{
		const { tab, flows, gateway, snackbar } = await create();
		tab.add();
		expect( tab.problem() ).toBe( 'A value has a name, its key.' );
		tab.set( 'name', ' limit ' );
		tab.set( 'scope', 'node' );
		expect( tab.problem() ).toBe( 'Choose the node.' );
		tab.set( 'nodeId', 'fn' );
		expect( tab.problem() ).toContain( 'The value is json' );
		tab.set( 'text', '{high' );
		expect( tab.problem() ).toContain( 'Not json' );
		await tab.save();
		expect( flows.setContext ).not.toHaveBeenCalled();
		tab.set( 'text', '{"high": 9}' );
		flows.setContext.mockRejectedValueOnce( new Error( 'no' ) );
		await tab.save();
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not set the value.', expect.anything() );
		expect( tab.editing() ).toBeDefined();//kept, to try again
		await tab.save();
		expect( flows.setContext ).toHaveBeenLastCalledWith( gateway, {flowId: 7, nodeId: 'fn', name: 'limit'}, {high: 9} );
		expect( [contextValueProblem( 'null' ), contextValueProblem( '"text"' ), contextValueProblem( '[1, 2]' )] ).toEqual( ['null takes a key away:  Remove does that.', undefined, undefined] );
	} );
	it( 'removes a value once confirmed', async ()=>{
		const { tab, flows, gateway, dialog } = await create( false );
		await tab.remove( rows[1] );
		expect( flows.setContext ).not.toHaveBeenCalled();
		dialog.answer = true;
		await tab.remove( rows[1] );
		expect( flows.setContext ).toHaveBeenLastCalledWith( gateway, {flowId: 7, nodeId: undefined, name: 'limits'}, undefined );
		expect( (<{message:string}>dialog.open.mock.lastCall![1]!.data).message ).toContain( "The key leaves the flow's context" );
	} );
} );