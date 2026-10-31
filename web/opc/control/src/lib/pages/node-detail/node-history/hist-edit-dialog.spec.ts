import { TestBed } from '@angular/core/testing';
import Long from 'long';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { DateUtils } from 'jde-framework';
import { HistEditArgs, HistEditResult } from '../../../model/hist';
import { OpcError } from '../../../model/opc-error';
import { EAccess, ETypes, StatusCode } from '../../../model/types';
import { HistEditDialog, HistEditDialogData, parseStatus } from './hist-edit-dialog';
import { variable } from 'jde-opc/testing';

const historized = ( i:number, name:string, dataType:ETypes )=>variable( i, name, {dataType: {i: dataType}, historizing: true, userAccessLevel: EAccess.All} );
const Rpm = historized( 1, 'rpm', ETypes.Double ), Running = historized( 2, 'running', ETypes.Boolean );
//The dialog over a stub editor:  `edits` is what it sent, `answerWith` sets the result each edit gets, `closed` what it closed with
//and `names` the status codes it asked the names of.
const open = ( data:Partial<HistEditDialogData>&{kind:HistEditDialogData["kind"]} )=>{
	let answer:HistEditResult|Error = { values: [], nodes: [] };
	const edit = vi.fn( async ( _args:HistEditArgs )=>{ if( answer instanceof Error ) throw answer; return answer; } );
	const names = vi.fn( ( _codes:StatusCode[] )=>{} );
	const ref = { disableClose: <boolean|undefined>undefined, close: vi.fn( ( _r?:HistEditResult )=>{} ) };
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
		{ provide: MAT_DIALOG_DATA, useValue: <HistEditDialogData>{ nodes: [Rpm, Running], edit, names, ...data } }
	]});
	const fixture = TestBed.createComponent( HistEditDialog );
	fixture.detectChanges();
	return { fixture, dialog: fixture.componentInstance, ref,
		edits: ()=>edit.mock.calls.map( ([args])=>args ),
		closed: ()=>ref.close.mock.calls.map( ([r])=>r ),
		names: ()=>names.mock.calls.map( ([codes])=>codes ),
		answerWith: ( a:HistEditResult|Error )=>{ answer = a; } };
};

describe( 'parseStatus', ()=>{
	it( 'takes hex or decimal, blank as Good, and nothing else', ()=>{
		expect( parseStatus( '' ) ).toBeUndefined();
		expect( parseStatus( ' 0x40940600 ' ) ).toBe( 0x40940600 );
		expect( parseStatus( '1082130432' ) ).toBe( 0x40800000 );
		expect( parseStatus( 'Good' ) ).toBeNull();
		expect( parseStatus( '0x100000000' ) ).toBeNull();
	} );
} );

describe( 'HistEditDialog', ()=>{
	it( 'opens a Replace on the row it was given and sends its node, time, value and the status typed', async ()=>{
		const time = new Date( 2026, 9, 9, 10, 0, 0, 250 );
		const d = open( {kind: 'replace', preset: {node: Rpm, time, value: 1500}} );
		expect( d.dialog.node() ).toBe( Rpm );
		expect( d.dialog.time() ).toBe( DateUtils.toLocalInput( time ) );
		expect( d.dialog.text() ).toBe( '1500' );
		expect( d.dialog.status() ).toBe( '' );
		expect( d.dialog.problem() ).toBeUndefined();
		d.dialog.text.set( '1600' );
		d.dialog.status.set( '0x40000000' );
		d.answerWith( {values: [{node: Rpm.nodeId, source: time, status: 0x00A60000}], nodes: [{node: Rpm.nodeId, status: 0}]} );//Good_EntryReplaced
		await d.dialog.apply();
		expect( d.edits() ).toEqual( [{kind: 'replace', values: [{node: Rpm.nodeId, source: time, value: 1600, status: 0x40000000}]}] );
		expect( d.closed() ).toHaveLength( 1 );
		expect( d.closed()[0]?.values[0].status ).toBe( 0x00A60000 );
	} );
	//The time field holds milliseconds, so a Replace from a row sent a time with no record (historian-web-edits #1).  The row's
	//own time goes while the field shows it, and what the user types once they change it.
	it( "sends a row's time to the tick while the field still shows it", async ()=>{
		const time = new Date( 1700000000000 ), sourceTime = {seconds: 1700000000, nanos: 123400};
		const d = open( {kind: 'replace', preset: {node: Rpm, time, sourceTime, value: 1500}} );
		d.dialog.text.set( '1600' );
		await d.dialog.apply();
		expect( d.edits()[0] ).toEqual( {kind: 'replace', values: [{node: Rpm.nodeId, source: sourceTime, value: 1600}]} );
		const later = new Date( 1700000001000 );
		d.dialog.time.set( DateUtils.toLocalInput( later ) );
		await d.dialog.apply();
		expect( d.edits()[1] ).toEqual( {kind: 'replace', values: [{node: Rpm.nodeId, source: later, value: 1600}]} );
	} );
	it( 'cannot send without a node, a time and a value, and says which', ()=>{
		const d = open( {kind: 'insert'} );
		expect( d.dialog.problem() ).toBe( 'Pick a node.' );
		d.dialog.onNodes( Rpm );
		expect( d.dialog.problem() ).toBe( 'Enter a value.' );
		d.dialog.text.set( 'abc' );
		expect( d.dialog.problem() ).toBe( 'Enter a value.' );//not a number for a Double
		d.dialog.text.set( '2' );
		d.dialog.status.set( 'Good' );
		expect( d.dialog.problem() ).toBe( "The status isn't a code - hex with 0x, or decimal." );
		d.dialog.status.set( '' );
		d.dialog.time.set( '' );
		expect( d.dialog.problem() ).toBe( 'Enter the source time.' );
		expect( d.dialog.args() ).toBeUndefined();
	} );
	//reviews-issue/412-clients-web-fixes.md #3:  a 64-bit integer went as Number(text), which holds 53 bits
	it( 'sends a 64-bit integer as a Long from the text, exact past 2^53', ()=>{
		const big = historized( 3, 'count', ETypes.Int64 );
		const d = open( {kind: 'replace', nodes: [big], preset: {node: big, time: new Date(1000)}} );
		d.dialog.text.set( '9007199254740993' );
		const value = d.dialog.value();
		expect( value ).toBeInstanceOf( Long );
		expect( value!.toString() ).toBe( '9007199254740993' );
		d.dialog.text.set( '1.5' );
		expect( d.dialog.problem() ).toBe( 'Enter a value.' );
	} );
	it( 'takes a Boolean as a toggle, false included', ()=>{
		const d = open( {kind: 'update', preset: {node: Running, time: new Date(1000)}} );
		expect( d.dialog.editor() ).toBe( 'boolean' );
		expect( d.dialog.problem() ).toBeUndefined();
		expect( d.dialog.args() ).toEqual( {kind: 'update', values: [{node: Running.nodeId, source: new Date(1000), value: false}]} );
	} );
	//a value the server refused shows beside the fields, by name once the name arrives, and the dialog stays open for another go
	it( 'stays open on a refusal and shows it', async ()=>{
		const d = open( {kind: 'insert', preset: {node: Rpm, time: new Date(2000), value: 1}} );
		d.answerWith( {values: [{node: Rpm.nodeId, source: new Date(2000), status: 0x80A30000}], nodes: [{node: Rpm.nodeId, status: 0}]} );//Bad_EntryExists
		await d.dialog.apply();
		expect( d.closed() ).toHaveLength( 0 );
		expect( d.dialog.rows() ).toEqual( [{node: 'rpm', time: new Date(2000), status: 0x80A30000}] );
		expect( d.names() ).toEqual( [[0x80A30000, 0]] );
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( '.results li' )?.textContent ).toContain( 'rpm at' );
		OpcError.setMessages( [{sc: 0x80A30000, message: 'BadEntryExists'}] );
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( '.results li' )?.textContent ).toContain( 'BadEntryExists' );
		//the next go clears it
		d.answerWith( {values: [{node: Rpm.nodeId, source: new Date(2000), status: 0x00A20000}], nodes: []} );
		await d.dialog.apply();
		expect( d.dialog.rows() ).toEqual( [] );
		expect( d.closed() ).toHaveLength( 1 );
	} );
	//Escape or a click outside closed it while the edit was out:  the server took the edit, and the tab never read it again
	//(historian-web-edits #4)
	it( 'cannot be closed but by its answer while the edit is out', async ()=>{
		let answer!:( r:HistEditResult )=>void;
		const d = open( {kind: 'insert', preset: {node: Rpm, time: new Date(2000), value: 1}, edit: ()=>new Promise<HistEditResult>( r=>answer = r )} );
		const applied = d.dialog.apply();
		expect( d.ref.disableClose ).toBe( true );
		answer( {values: [{node: Rpm.nodeId, source: new Date(2000), status: 0x809F0000}], nodes: []} );//Bad_EntryExists:  open for another go
		await applied;
		expect( d.ref.disableClose ).toBe( false );
		expect( d.closed() ).toHaveLength( 0 );
	} );
	//the gateway's refusal of the call - a value of the wrong type, say - is a throw, shown as its text
	it( 'shows the gateway\'s refusal of the arguments', async ()=>{
		const d = open( {kind: 'insert', preset: {node: Rpm, time: new Date(2000), value: 1}} );
		d.answerWith( new Error( "createHistory:  {ns:2, i:1}'s value \"x\" isn't a Double" ) );
		await d.dialog.apply();
		expect( d.dialog.error() ).toContain( "isn't a Double" );
		expect( d.closed() ).toHaveLength( 0 );
		expect( d.dialog.busy() ).toBe( false );
	} );
	it( 'purges a range over several nodes, start before end, and answers per node', async ()=>{
		const d = open( {kind: 'purgeRange', preset: {time: new Date(2026, 0, 1, 0, 0, 0, 0), end: new Date(2026, 0, 2, 0, 0, 0, 0)}} );
		expect( d.dialog.problem() ).toBe( 'Pick the nodes.' );
		d.dialog.onNodes( [Rpm, Running] );
		expect( d.dialog.problem() ).toBeUndefined();
		d.dialog.end.set( DateUtils.toLocalInput( new Date(2025, 0, 1) ) );
		expect( d.dialog.problem() ).toBe( 'The start is after the end.' );
		d.dialog.end.set( d.dialog.time() );//start equal to end:  the one value there
		expect( d.dialog.args() ).toEqual( {kind: 'purgeRange', nodes: [Rpm.nodeId, Running.nodeId], start: new Date(2026, 0, 1), end: new Date(2026, 0, 1)} );
		//one node with nothing in the range:  the other's values are gone, so the dialog closes, and the tab says what was refused
		//(historian-web-edits #2)
		const partly = {values: [], nodes: [{node: Rpm.nodeId, status: 0}, {node: Running.nodeId, status: 0x809B0000}]};//Bad_NoData
		d.answerWith( partly );
		await d.dialog.apply();
		expect( d.closed() ).toEqual( [partly] );
	} );
	it( 'stays open when every node refused a range purge, and lists each', async ()=>{
		const d = open( {kind: 'purgeRange', preset: {time: new Date(2026, 0, 1)}} );
		d.dialog.onNodes( [Rpm, Running] );
		d.answerWith( {values: [], nodes: [{node: Rpm.nodeId, status: 0x809B0000}, {node: Running.nodeId, status: 0x809B0000}]} );
		await d.dialog.apply();
		expect( d.closed() ).toHaveLength( 0 );
		expect( d.dialog.rows().map( r=>[r.node, r.status] ) ).toEqual( [['rpm', 0x809B0000], ['running', 0x809B0000]] );
	} );
	//Every line had the error icon, Good ones included (historian-web-edits #10).
	it( "gives each line of the answer its status's icon", ()=>{
		const d = open( {kind: 'purgeRange', preset: {time: new Date(2026, 0, 1)}} );
		d.dialog.result.set( {values: [], nodes: [{node: Rpm.nodeId, status: 0}, {node: Running.nodeId, status: 0x809B0000}, {node: Rpm.nodeId, status: 0x40000000}]} );
		d.fixture.detectChanges();
		const icons = [...d.fixture.nativeElement.querySelectorAll( '.results li' )].map( li=>li.querySelector( 'mat-icon' )?.getAttribute( 'fontIcon' ) ?? li.querySelector( 'mat-icon' )?.textContent?.trim() ?? null );
		expect( icons ).toEqual( [null, 'error', 'warning'] );
	} );
	it( 'purges at the times listed, one at least', ()=>{
		const d = open( {kind: 'purgeTimes', preset: {node: Rpm, time: new Date(2026, 0, 1)}} );
		d.dialog.addTime();
		d.dialog.setTime( 1, DateUtils.toLocalInput( new Date(2026, 0, 3) ) );
		expect( d.dialog.args() ).toEqual( {kind: 'purgeTimes', nodes: [Rpm.nodeId], times: [new Date(2026, 0, 1), new Date(2026, 0, 3)]} );
		d.dialog.removeTime( 0 );
		d.dialog.removeTime( 0 );//the last one stays
		expect( d.dialog.times() ).toEqual( [DateUtils.toLocalInput( new Date(2026, 0, 3) )] );
		d.dialog.setTime( 0, '' );
		expect( d.dialog.problem() ).toBe( 'Enter each time.' );
	} );
	it( 'renders the fields of its kind', ()=>{
		const d = open( {kind: 'purgeRange'} );
		const labels = ()=>[...d.fixture.nativeElement.querySelectorAll( 'mat-label' )].map( l=>l.textContent.trim() );
		expect( labels() ).toEqual( ['Nodes', 'Start', 'End'] );
		expect( d.fixture.nativeElement.querySelector( 'h2' )?.textContent ).toBe( 'Delete a range' );
		expect( d.fixture.nativeElement.querySelector( 'mat-dialog-actions button:last-child' )?.textContent?.trim() ).toBe( 'Delete' );
	} );
} );