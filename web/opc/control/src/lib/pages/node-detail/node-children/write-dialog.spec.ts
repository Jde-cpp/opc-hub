import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import Long from 'long';
import { Enum } from '../../../model/enum';
import { NodeId } from '../../../model/node-id';
import { EAccess, ETypes } from '../../../model/types';
import { Reading, Value } from '../../../model/value';
import { ValueEditor } from '../value-editor/value-editor';
import { indexRangeOf, WriteDialog, WriteDialogData } from './write-dialog';
import { variable } from 'jde-opc/testing';

const writable = ( i:number, name:string, json:object )=>variable( i, name, {userAccessLevel: EAccess.Read|EAccess.Write, ...json} );
//The dialog over a stub gateway:  `writes` is what it sent, `answerWith` the reading each write gets, `closed` what it closed with
const open = ( data:Partial<WriteDialogData>&{node:WriteDialogData["node"]} )=>{
	let answer:Reading|Error = { value: data.node.value, sc: 0 };
	const write = vi.fn( async ( _value:Value, _indexRange?:string )=>{ if( answer instanceof Error ) throw answer; return answer; } );
	const ref = { disableClose: <boolean|undefined>undefined, close: vi.fn( ( _r?:Reading )=>{} ) };
	TestBed.resetTestingModule();//a second dialog in one test
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
		{ provide: MAT_DIALOG_DATA, useValue: <WriteDialogData>{ write, ...data } }
	]});
	const fixture = TestBed.createComponent( WriteDialog );
	fixture.detectChanges();
	const dialog = fixture.componentInstance;
	return { fixture, dialog, ref,
		editor: ()=><ValueEditor>fixture.debugElement.query( By.directive( ValueEditor ) ).componentInstance,
		writes: ()=>write.mock.calls,
		closed: ()=>ref.close.mock.calls.map( ([r])=>r ),
		answerWith: ( a:Reading|Error )=>{ answer = a; } };
};

//Part 4 §7.27, one dimension:  an index or low:high, and the elements it names of an array of the length
describe( 'indexRangeOf', ()=>{
	it( 'reads an index or a range, and counts the elements in the array', ()=>{
		expect( indexRangeOf( '2', 5 ) ).toEqual( {start: 2, count: 1} );
		expect( indexRangeOf( ' 1:3 ', 5 ) ).toEqual( {start: 1, count: 3} );
		expect( indexRangeOf( '3:9', 5 ) ).toEqual( {start: 3, count: 2} );//to the array's end
		expect( indexRangeOf( '5', 5 ) ).toEqual( {start: 5, count: 0} );//past it
		expect( indexRangeOf( '3:1', 5 ) ).toBeUndefined();
		expect( indexRangeOf( '2:2', 5 ) ).toBeUndefined();//low is less than high, Part 4
		expect( indexRangeOf( 'x', 5 ) ).toBeUndefined();
		expect( indexRangeOf( '1,2', 5 ) ).toBeUndefined();//one dimension here
	} );
} );

//clients spec *Values and writes* (#423):  the cell editors gave way to a dialog, one editor per built-in type (ValueEditor), opened
//on the row's reading, an array whole or by index range
describe( 'WriteDialog', ()=>{
	it( 'opens on the row\'s value, typed as its DataType says, and closes with the reading the write answers', async ()=>{
		const d = open( {node: writable( 1, 'rpm', {dataType: {i: ETypes.Double}, value: 1480.5})} );
		expect( d.dialog.title ).toBe( 'Write rpm' );
		expect( d.dialog.typeName ).toBe( 'Double' );
		expect( d.editor().kind() ).toBe( 'number' );
		expect( d.editor().text() ).toBe( '1480.5' );
		expect( d.dialog.problem() ).toBeUndefined();
		d.editor().setText( '1500' );
		d.answerWith( {value: 1500, sc: 0x40000000} );
		await d.dialog.apply();
		expect( d.writes() ).toEqual( [[1500, undefined]] );
		expect( d.closed() ).toEqual( [{value: 1500, sc: 0x40000000}] );
	} );
	it( 'sends nothing until the fields hold a value, and says so', async ()=>{
		const d = open( {node: writable( 2, 'count', {dataType: {i: ETypes.Int64}})} );
		expect( d.dialog.problem() ).toBe( 'Enter a value.' );
		await d.dialog.apply();
		expect( d.writes() ).toHaveLength( 0 );
		d.editor().setText( '9007199254740993' );
		expect( d.dialog.problem() ).toBeUndefined();
		await d.dialog.apply();
		expect( d.writes()[0][0] ).toBeInstanceOf( Long );
		expect( d.writes()[0][0].toString() ).toBe( '9007199254740993' );
	} );
	it( 'stays open on a refusal and shows it, closed only by its answer while the write is out', async ()=>{
		let answer!:( r:Reading )=>void;
		const held = open( {node: writable( 3, 'x', {dataType: {i: ETypes.String}, value: 'a'}), write: ()=>new Promise<Reading>( r=>answer = r )} );
		const applied = held.dialog.apply();
		expect( held.ref.disableClose ).toBe( true );
		answer( {value: 'a', sc: 0} );
		await applied;
		expect( held.ref.disableClose ).toBe( false );
		expect( held.closed() ).toHaveLength( 1 );
		const d = open( {node: writable( 3, 'x', {dataType: {i: ETypes.String}, value: 'a'})} );
		d.answerWith( new Error( "BadUserAccessDenied" ) );
		await d.dialog.apply();
		expect( d.dialog.error() ).toBe( 'BadUserAccessDenied' );
		expect( d.closed() ).toHaveLength( 0 );
		expect( d.dialog.busy() ).toBe( false );
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( '.refused' )?.textContent ).toContain( 'BadUserAccessDenied' );
	} );
	it( 'offers an enumeration\'s values for a type of the server\'s own', ()=>{
		const health = writable( 4, 'health', {dataType: {ns: 2, i: 3002}, value: 1} );
		health.customDataType = new Enum( new NodeId( {ns: 2, i: 3002} ), <any>{name: 'Health', enumValues: [{id: 1, name: 'OK'}, {id: 2, name: 'Failed'}]} );
		const d = open( {node: health} );
		expect( d.dialog.typeName ).toBe( 'Health' );
		expect( d.editor().kind() ).toBe( 'enum' );
		expect( d.editor().choice() ).toBe( 1 );
	} );
	it( 'edits a structure member by member, and offers no Write for a body no type explains', ()=>{
		const sample = writable( 5, 'sample', {dataType: {ns: 2, i: 3001}, value: {Rpm: 1480.5, Running: true}} );
		const d = open( {node: sample} );
		expect( d.editor().kind() ).toBe( 'structure' );
		expect( d.dialog.problem() ).toBeUndefined();
		expect( d.dialog.value() ).toEqual( {Rpm: 1480.5, Running: true} );
	} );
	//an array whole, or the slice an index range names - prefilled from the elements it has, edited at its own length - and the
	//range sent with it
	it( 'writes an array whole, or the slice an index range names', async ()=>{
		const d = open( {node: writable( 6, 'levels', {dataType: {i: ETypes.Int32}, valueRank: 1, value: [1, 2, 3, 4, 5]})} );
		expect( d.dialog.isArray ).toBe( true );
		expect( d.dialog.typeName ).toBe( 'Int32[]' );
		expect( d.editor().kind() ).toBe( 'array' );
		expect( d.dialog.value() ).toEqual( [1, 2, 3, 4, 5] );
		d.dialog.setIndexRange( '1:2' );
		expect( d.dialog.value() ).toEqual( [2, 3] );
		expect( d.dialog.problem() ).toBeUndefined();
		d.fixture.detectChanges();
		expect( d.editor().fixedLength() ).toBe( true );
		expect( d.editor().elements() ).toEqual( [2, 3] );
		d.editor().setElement( 0, 20 );
		d.answerWith( {value: [1, 20, 3, 4, 5], sc: 0} );
		await d.dialog.apply();
		expect( d.writes() ).toEqual( [[[20, 3], '1:2']] );
		d.dialog.setIndexRange( 'x' );
		expect( d.dialog.problem() ).toBe( 'The index range is an index, or low:high.' );
		d.dialog.setIndexRange( '7' );
		expect( d.dialog.problem() ).toBe( 'The index range is past the array.' );
		d.dialog.setIndexRange( '' );
		expect( d.dialog.value() ).toEqual( [1, 2, 3, 4, 5] );
		expect( d.dialog.problem() ).toBeUndefined();
		d.dialog.setIndexRange( '3:9' );//reviews-issue/423-clients-writes.md #5:  past the end, the range sent is the slice's, the size of the value
		expect( d.dialog.value() ).toEqual( [4, 5] );
		await d.dialog.apply();
		d.dialog.setIndexRange( '4' );
		await d.dialog.apply();
		expect( d.writes().slice( 1 ) ).toEqual( [[[4, 5], '3:4'], [[5], '4']] );
	} );
	it( 'renders the index range field for an array alone', ()=>{
		const scalar = open( {node: writable( 7, 'rpm', {dataType: {i: ETypes.Double}, value: 1})} );
		expect( scalar.fixture.nativeElement.querySelector( 'input[placeholder="whole array"]' ) ).toBeNull();
		const array = open( {node: writable( 8, 'levels', {dataType: {i: ETypes.Int32}, valueRank: 1, value: [1]})} );
		expect( array.fixture.nativeElement.querySelector( 'input[placeholder="whole array"]' ) ).not.toBeNull();
		expect( array.fixture.nativeElement.querySelector( 'mat-dialog-actions button:last-child' )?.textContent?.trim() ).toBe( 'Write' );
	} );
} );