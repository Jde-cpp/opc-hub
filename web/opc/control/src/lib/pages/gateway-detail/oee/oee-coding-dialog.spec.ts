import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { DateUtils } from 'jde-framework';
import { OeeCodingCall, OeeReasonRef } from '../../../model/oee';
import { OeeCodingDialog, OeeCodingDialogData, OeeCodingStop } from './oee-coding-dialog';

const Jam = {id: 3, name: 'Jam', category: 'Unplanned'}, NoFilm = {id: 4, name: 'No film', category: 'Unplanned'};
const stop:OeeCodingStop = { start: new Date( '2026-11-11T08:12:00.123Z' ), startText: '2026-11-11T08:12:00.1234567Z', end: new Date( '2026-11-11T08:40:00Z' ), endText: '2026-11-11T08:40:00.0000000Z', codings: [] };
const coding = ( id:number, reason:OeeReasonRef, extra:object={} )=>({ id, start: stop.start, end: stop.end!, reason, userName: 'op', ...extra });
//The dialog over stubs:  `calls` is what it sent to codeOee, `refuse` has the nth call throw, `removed` the ids it uncoded and
//`closed` what it closed with.
const open = ( data:Partial<OeeCodingDialogData>={} )=>{
	let refused:number|undefined;
	const code = vi.fn( async ( _call:OeeCodingCall )=>{ if( code.mock.calls.length==refused ) throw new Error( 'Coding 8 already covers it.' ); } );
	const uncode = vi.fn( async ( _id:number )=>{} );
	const ref = { disableClose: <boolean|undefined>undefined, close: vi.fn( ( _r?:boolean )=>{} ) };
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
		{ provide: MAT_DIALOG_DATA, useValue: <OeeCodingDialogData>{ stop, reasons: [Jam, NoFilm], code, uncode, ...data } }
	]});
	const fixture = TestBed.createComponent( OeeCodingDialog );
	fixture.detectChanges();
	return { fixture, dialog: fixture.componentInstance, ref,
		calls: ()=>code.mock.calls.map( ([call])=>call ),
		removed: ()=>uncode.mock.calls.map( ([id])=>id ),
		closed: ()=>ref.close.mock.calls.map( ([r])=>r ),
		refuse: ( nth:number )=>{ refused = nth; } };
};

describe( 'OeeCodingDialog', ()=>{
	it( 'codes the stop once with the reason and the comment, and closes', async ()=>{
		const d = open();
		expect( d.dialog.problem() ).toBe( 'Pick a reason.' );
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [] );
		d.dialog.reason.set( 3 );
		d.dialog.comment.set( 'film' );
		expect( d.dialog.problem() ).toBeUndefined();
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [{start: stop.startText, end: stop.endText, reason: 3, comment: 'film'}] );
		expect( d.closed() ).toEqual( [true] );
		expect( d.ref.disableClose ).toBe( false );
	} );
	it( 'splits a stop into two calls that meet at the split time', async ()=>{
		const d = open();
		d.dialog.reason.set( 3 );
		d.dialog.split.set( true );
		expect( DateUtils.fromLocalInput( d.dialog.splitAt() ) ).toEqual( new Date( ( stop.start.getTime()+stop.end!.getTime() )/2 ) );//opens on the middle
		expect( d.dialog.problem() ).toBe( 'Pick the reason after the split.' );
		d.dialog.reason2.set( 4 );
		d.dialog.comment2.set( 'ran out' );
		const at = new Date( '2026-11-11T08:20:00Z' );
		d.dialog.splitAt.set( DateUtils.toLocalInput( at ) );
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [
			{start: stop.startText, end: at.toISOString(), reason: 3},
			{start: at.toISOString(), end: stop.endText, reason: 4, comment: 'ran out'}] );
		expect( d.closed() ).toEqual( [true] );
	} );
	it( 'takes a split time only inside the stop', ()=>{
		const d = open();
		d.dialog.reason.set( 3 );
		d.dialog.reason2.set( 4 );
		d.dialog.split.set( true );
		d.dialog.splitAt.set( DateUtils.toLocalInput( new Date( '2026-11-11T08:45:00Z' ) ) );
		expect( d.dialog.problem() ).toMatch( /inside the stop/ );
		expect( d.dialog.calls() ).toBeUndefined();
		d.dialog.splitAt.set( '' );
		expect( d.dialog.problem() ).toBe( 'Enter the split time.' );
	} );
	it( 'stays open on a refusal, and says which part of a split was taken', async ()=>{
		const d = open();
		d.dialog.reason.set( 3 );
		d.refuse( 1 );
		await d.dialog.apply();
		expect( d.dialog.error() ).toBe( 'Coding 8 already covers it.' );
		expect( d.closed() ).toEqual( [] );
		d.dialog.split.set( true );
		d.dialog.reason2.set( 4 );
		d.refuse( 3 );//the second of the two
		await d.dialog.apply();
		expect( d.calls() ).toHaveLength( 3 );
		expect( d.dialog.error() ).toBe( 'The part before the split is coded;  the part after it was refused:  Coding 8 already covers it.' );
		expect( d.closed() ).toEqual( [] );
		d.refuse( 0 );
		await d.dialog.apply();//both again:  the first replaces itself
		expect( d.calls() ).toHaveLength( 5 );
		expect( d.dialog.error() ).toBeUndefined();
		expect( d.closed() ).toEqual( [true] );
	} );
	it( "opens on the stop's one coding, and on none where it has two or its reason is deleted", ()=>{
		const one = open( {stop: {...stop, codings: [coding( 8, Jam, {comment: 'film'} )]}} );
		expect( [one.dialog.reason(), one.dialog.comment()] ).toEqual( [3, 'film'] );
		TestBed.resetTestingModule();//a second dialog in the one test
		const two = open( {stop: {...stop, codings: [coding( 8, Jam ), coding( 9, NoFilm )]}} );
		expect( two.dialog.reason() ).toBeUndefined();
		TestBed.resetTestingModule();
		const gone = open( {stop: {...stop, codings: [coding( 8, {id: 77, name: 'Old', category: 'Planned'} )]}} );
		expect( gone.dialog.reason() ).toBeUndefined();
	} );
	it( 'removes a coding over the stop and takes it off its list', async ()=>{
		const d = open( {stop: {...stop, codings: [coding( 8, Jam ), coding( 9, NoFilm )]}} );
		expect( d.fixture.nativeElement.querySelectorAll( '.codings li' ) ).toHaveLength( 2 );
		await d.dialog.remove( d.dialog.codings()[1] );
		expect( d.removed() ).toEqual( [9] );
		expect( d.dialog.codings().map( c=>c.id ) ).toEqual( [8] );
		expect( d.closed() ).toEqual( [] );
	} );
	it( 'codes an open stop to now', async ()=>{
		const d = open( {stop: {start: stop.start, startText: stop.startText, codings: []}} );
		d.dialog.reason.set( 3 );
		const before = Date.now();
		await d.dialog.apply();
		const [call] = d.calls();
		expect( call.start ).toBe( stop.startText );
		expect( new Date( call.end ).getTime() ).toBeGreaterThanOrEqual( before );
		expect( d.fixture.nativeElement.querySelector( '.hint' ).textContent ).toContain( 'still open' );
	} );
} );