import { Clipboard } from '@angular/cdk/clipboard';
import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { SnackbarService } from 'jde-framework';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';
import { Flow, FlowGraph, FlowRun, FlowVersion } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { FlowReplayAsked, localInput, replayProblem } from './flow-replay-dialog';
import { FlowVersions } from './flow-versions';

const graph = ( ...ids:string[] ):FlowGraph=>({ nodes: ids.map( id=>({id, type: 'debug', name: '', x: 0, y: 0, props: {}, wires: []}) ) });
const version = ( n:number, g:FlowGraph, comment?:string ):FlowVersion=>({ id: n, version: n, graph: g, identityId: 1, userName: 'ann', comment });
const versions = [version( 3, graph( 'a', 'c' ), 'c for b' ), version( 2, graph( 'a', 'b' ) ), version( 1, graph( 'a' ), 'the first' )];
const run = ( id:number, status?:string ):FlowRun=>({ id, version: 3, mode: 'dry', userName: 'ann', status });

const create = async ( answer?:FlowReplayAsked )=>{
	const flows = {
		runs: vi.fn( async ( ..._args:unknown[] )=>[run( 2 ), run( 1, 'completed' )] ),
		replay: vi.fn( async ( ..._args:unknown[] )=>run( 3 ) ),
		cancelRun: vi.fn( async ( ..._args:unknown[] )=>{} )
	};
	const dialog = dialogStub( answer ), snackbar = snackbarStub(), clipboard = { copy: vi.fn( ( _text:string )=>true ) }, gateway = { query: vi.fn(), postQL: vi.fn() };
	TestBed.configureTestingModule( {providers: [{ provide: FlowService, useValue: flows }, { provide: MatDialog, useValue: dialog }, { provide: SnackbarService, useValue: snackbar }, { provide: Clipboard, useValue: clipboard }]} );
	const fixture = TestBed.createComponent( FlowVersions ), tab = fixture.componentInstance;
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'flow', new Flow( {id: 7, name: 'Pump', slug: 'pump', version: 3} ) );
	fixture.componentRef.setInput( 'versions', versions );
	const opened:FlowVersion[] = [], openedRuns:FlowRun[] = [];
	tab.open.subscribe( v=>opened.push( v ) );
	tab.openRun.subscribe( r=>openedRuns.push( r ) );
	await stable( fixture );
	return { fixture, tab, element: <HTMLElement>fixture.nativeElement, flows, dialog, snackbar, clipboard, gateway, opened, openedRuns };
};

describe( 'FlowVersions', ()=>{
	it( 'lists the versions, the current one marked', async ()=>{
		const { element, tab } = await create();
		const rows = [...element.querySelectorAll( 'mat-table' )[0].querySelectorAll( 'mat-row' )];
		expect( rows.map( r=>r.querySelector( '.mat-column-version' )!.textContent!.replace( /\s+/g, ' ' ).trim() ) ).toEqual( ['3 current', '2', '1'] );
		expect( rows.map( r=>r.querySelector( '.mat-column-comment' )!.textContent!.trim() ) ).toEqual( ['c for b', 'none', 'the first'] );
		expect( tab.version( 2 )?.graph ).toEqual( graph( 'a', 'b' ) );
	} );
	//the current version against the one before it, until another pair is chosen
	it( 'compares two versions by node', async ()=>{
		const { tab, element, fixture } = await create();
		expect( [tab.from(), tab.to()] ).toEqual( [2, 3] );
		expect( tab.changes().map( c=>[c.kind, c.id] ) ).toEqual( [['added', 'c'], ['removed', 'b']] );
		expect( [...element.querySelectorAll( '.changes li' )].map( li=>li.className ) ).toEqual( ['added', 'removed'] );
		tab.from.set( 1 );
		await stable( fixture );
		expect( tab.changes().map( c=>[c.kind, c.id] ) ).toEqual( [['added', 'c']] );
		tab.to.set( 1 );
		await stable( fixture );
		expect( element.querySelector( '.compare .empty' )!.textContent ).toContain( 'A version against itself.' );
	} );
	it( "opens a version on the canvas, and copies one's json", async ()=>{
		const { element, opened, clipboard, snackbar } = await create();
		const buttons = [...element.querySelectorAll( 'mat-table' )[0].querySelectorAll( 'mat-row' )][1].querySelectorAll( 'button' );
		(<HTMLButtonElement>buttons[0]).click();
		(<HTMLButtonElement>buttons[1]).click();
		expect( opened ).toEqual( [versions[1]] );
		expect( JSON.parse( clipboard.copy.mock.lastCall![0] ) ).toEqual( {name: 'Pump', slug: 'pump', graph: graph( 'a', 'b' )} );
		expect( snackbar.info ).toHaveBeenCalledWith( "Version 2's json copied." );
	} );
	it( "lists the flow's replays, starts one and cancels one", async ()=>{
		const asked = { from: new Date( '2026-11-14T08:00:00Z' ), to: new Date( '2026-11-14T09:00:00Z' ), mode: <const>'commit' };
		const { tab, element, flows, gateway, dialog, snackbar, openedRuns } = await create( asked );
		expect( flows.runs ).toHaveBeenCalledWith( gateway, 7 );
		expect( tab.runs().map( r=>[r.id, tab.runState( r )] ) ).toEqual( [[2, 'pending'], [1, 'ok']] );
		expect( tab.runState( run( 4, 'failed:  a value its group refused' ) ) ).toBe( 'bad' );
		await tab.replay();
		expect( dialog.open.mock.lastCall![1]!.data ).toEqual( {name: 'Pump', version: 3} );
		expect( flows.replay ).toHaveBeenCalledWith( gateway, 7, asked.from, asked.to, 'commit' );
		expect( snackbar.info ).toHaveBeenCalledWith( 'Replay 3 started.' );
		await tab.cancel( tab.runs()[0] );
		expect( flows.cancelRun ).toHaveBeenCalledWith( gateway, 2 );
		(<HTMLButtonElement>element.querySelector( '.runs mat-row button[aria-label="Open replay 2 in History"]' )).click();
		expect( openedRuns.map( r=>r.id ) ).toEqual( [2] );
		expect( flows.runs ).toHaveBeenCalledTimes( 3 );//read again after each
		flows.replay.mockRejectedValue( new Error( 'A run is under way.' ) );
		await tab.replay();
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not start the replay.', expect.anything() );
		expect( tab.busy() ).toBe( false );
	} );
	//a deploy, an enable or a disable is a new row of the same flow, whose runs are as they were (reviews-issue/455-flows-web-editor.md #5)
	it( "reads the runs again for another flow alone", async ()=>{
		const { fixture, flows } = await create();
		fixture.componentRef.setInput( 'flow', new Flow( {id: 7, name: 'Pump', slug: 'pump', version: 4, enabled: false} ) );
		await stable( fixture );
		expect( flows.runs ).toHaveBeenCalledTimes( 1 );
		fixture.componentRef.setInput( 'flow', new Flow( {id: 8, name: 'Fan', slug: 'fan', version: 1} ) );
		await stable( fixture );
		expect( flows.runs ).toHaveBeenCalledTimes( 2 );
	} );
	it( "holds a replay's range to what is stored", ()=>{
		const now = new Date( 2026, 10, 14, 12, 0 );
		expect( replayProblem( '2026-11-14T10:00', '2026-11-14T11:00', now ) ).toBeUndefined();
		expect( replayProblem( '', '2026-11-14T11:00', now ) ).toBe( 'From is a time.' );
		expect( replayProblem( '2026-11-14T10:00', 'x', now ) ).toBe( 'To is a time.' );
		expect( replayProblem( '2026-11-14T11:00', '2026-11-14T11:00', now ) ).toBe( 'From is before To.' );
		expect( replayProblem( '2026-11-14T10:00', '2026-11-14T12:01', now ) ).toContain( 'To is no later than now' );
		expect( localInput( new Date( 2026, 0, 5, 6, 7 ) ) ).toBe( '2026-01-05T06:07' );
	} );
} );