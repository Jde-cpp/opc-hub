import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import Long from 'long';
import { DateUtils, Guid, ProtoUtils, Timestamp } from 'jde-framework';
import { Enum } from '../../../model/enum';
import { ExNodeId } from '../../../model/ex-node-id';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { ETypes } from '../../../model/types';
import { ExtensionObject } from '../../../model/value';
import { editorKind, inferKind, ValueEditor } from './value-editor';

const health = new Enum( new NodeId( {ns: 2, i: 3002} ), <any>{name: 'Health', enumValues: [{id: 1, name: 'OK'}, {id: 2, name: 'Failed'}]} );
//An editor over `inputs`, its model's emissions collected:  `value` is what it holds now, `emitted` each value it set
const create = ( inputs:Record<string,unknown>={} )=>{
	const fixture = TestBed.createComponent( ValueEditor );
	for( const [name, v] of Object.entries( inputs ) )
		fixture.componentRef.setInput( name, v );
	const emitted:unknown[] = [];
	fixture.componentInstance.value.subscribe( v=>emitted.push( v ) );
	fixture.detectChanges();
	const editor = fixture.componentInstance;
	return { fixture, editor, emitted,
		value: ()=>editor.value(),
		input: ( selector='input' )=><HTMLInputElement>fixture.nativeElement.querySelector( selector ),
		children: ()=>fixture.debugElement.queryAll( By.directive( ValueEditor ) ).map( d=><ValueEditor>d.componentInstance ).filter( e=>e!==editor ) };
};

//clients spec *Values and writes* (#423):  one editor per built-in type, picked by the type, or by the value's own shape where the type
//is the server's own or unknown
describe( 'editorKind', ()=>{
	it( 'picks the editor by the built-in type, an array and an enumeration first', ()=>{
		expect( editorKind( ETypes.Boolean, false, undefined, undefined ) ).toBe( 'boolean' );
		expect( editorKind( ETypes.Int32, false, undefined, undefined ) ).toBe( 'number' );
		expect( editorKind( ETypes.Duration, false, undefined, undefined ) ).toBe( 'number' );
		expect( editorKind( ETypes.Enumeration, false, undefined, undefined ) ).toBe( 'number' );//the values unknown:  the number it is sent as
		expect( editorKind( ETypes.String, false, undefined, undefined ) ).toBe( 'text' );
		expect( editorKind( ETypes.XmlElement, false, undefined, undefined ) ).toBe( 'xml' );
		expect( editorKind( ETypes.UtcTime, false, undefined, undefined ) ).toBe( 'datetime' );
		expect( editorKind( ETypes.Guid, false, undefined, undefined ) ).toBe( 'guid' );
		expect( editorKind( ETypes.ByteString, false, undefined, undefined ) ).toBe( 'byteString' );
		expect( editorKind( ETypes.NodeId, false, undefined, undefined ) ).toBe( 'nodeId' );
		expect( editorKind( ETypes.ExpandedNodeId, false, undefined, undefined ) ).toBe( 'expandedNodeId' );
		expect( editorKind( ETypes.StatusCode, false, undefined, undefined ) ).toBe( 'statusCode' );
		expect( editorKind( ETypes.QualifiedName, false, undefined, undefined ) ).toBe( 'qualifiedName' );
		expect( editorKind( ETypes.LocalizedText, false, undefined, undefined ) ).toBe( 'localizedText' );
		expect( editorKind( ETypes.Double, true, undefined, undefined ) ).toBe( 'array' );
		expect( editorKind( ETypes.Int32, false, health, undefined ) ).toBe( 'enum' );
		expect( editorKind( ETypes.Structure, false, undefined, {Rpm: 1} ) ).toBe( 'structure' );
		expect( editorKind( ETypes.BaseDataType, false, undefined, 7 ) ).toBe( 'number' );
	} );
	it( 'infers the kind from a value of no known type, by its shape', ()=>{
		expect( inferKind( true ) ).toBe( 'boolean' );
		expect( inferKind( 7 ) ).toBe( 'number' );
		expect( inferKind( Long.fromString( '9007199254740993' ) ) ).toBe( 'number' );
		expect( inferKind( {low: 1, high: 0, unsigned: true} ) ).toBe( 'number' );
		expect( inferKind( 'x' ) ).toBe( 'text' );
		expect( inferKind( new Guid( '12345678-1234-1234-1234-123456789abc' ) ) ).toBe( 'guid' );
		expect( inferKind( new Uint8Array( [1] ) ) ).toBe( 'byteString' );
		expect( inferKind( new NodeId( {ns: 2, i: 5} ) ) ).toBe( 'nodeId' );
		expect( inferKind( {ns: 2, i: 5} ) ).toBe( 'nodeId' );
		expect( inferKind( new ExNodeId( {nsu: 'urn:x', i: 5} ) ) ).toBe( 'expandedNodeId' );
		expect( inferKind( {nsu: 'urn:x', i: 5} ) ).toBe( 'expandedNodeId' );
		expect( inferKind( new OpcError( 0x80340000, '', '', undefined ) ) ).toBe( 'statusCode' );
		expect( inferKind( {ns: 2, name: 'Speed'} ) ).toBe( 'qualifiedName' );
		expect( inferKind( {seconds: 1, nanos: 0} ) ).toBe( 'datetime' );
		expect( inferKind( {Rpm: 1, Limits: {Low: 0, High: 1}} ) ).toBe( 'structure' );
		expect( inferKind( [1, 2] ) ).toBe( 'array' );
		expect( inferKind( new ExtensionObject( new NodeId( {ns: 2, i: 1} ) ) ) ).toBe( 'undecoded' );
		expect( inferKind( {typeId: {ns: 2, i: 1}, body: 'AAAA'} ) ).toBe( 'undecoded' );
		expect( inferKind( null ) ).toBe( 'json' );
		expect( inferKind( {} ) ).toBe( 'json' );
	} );
} );

describe( 'ValueEditor', ()=>{
	it( 'holds a Boolean as a toggle, false from the start', ()=>{
		const e = create( {type: ETypes.Boolean} );
		expect( e.editor.kind() ).toBe( 'boolean' );
		expect( e.value() ).toBe( false );//a toggle always holds a value
		e.editor.setFlag( true );
		expect( e.value() ).toBe( true );
	} );
	it( 'takes a number as typed, nothing for text that is none, and a 64-bit integer as a Long exact past 2^53', ()=>{
		const e = create( {type: ETypes.Double, value: 1.5} );
		expect( e.editor.text() ).toBe( '1.5' );
		e.editor.setText( '2.25' );
		expect( e.value() ).toBe( 2.25 );
		e.editor.setText( 'abc' );
		expect( e.value() ).toBeUndefined();
		expect( e.editor.invalid() ).toBe( 'A number.' );
		e.editor.setText( '' );
		expect( e.value() ).toBeUndefined();
		expect( e.editor.invalid() ).toBeUndefined();//blank is nothing, not wrong
		const big = create( {type: ETypes.Int64} );
		big.editor.setText( '9007199254740993' );
		expect( big.value() ).toBeInstanceOf( Long );
		expect( big.value()!.toString() ).toBe( '9007199254740993' );
		big.editor.setText( '1.5' );
		expect( big.value() ).toBeUndefined();
		expect( big.editor.invalid() ).toContain( 'whole number' );
		//a Long held, of no known type:  its signedness picks the range
		const held = create( {value: Long.fromString( '18446744073709551615', true )} );
		expect( held.editor.numberType() ).toBe( ETypes.UInt64 );
		expect( held.editor.text() ).toBe( '18446744073709551615' );
		held.editor.setText( '-1' );
		expect( held.value() ).toBeUndefined();
	} );
	it( 'offers an enumeration\'s values and holds the one chosen', ()=>{
		const e = create( {type: ETypes.None, enumeration: health, value: 1} );
		expect( e.editor.kind() ).toBe( 'enum' );
		expect( e.editor.choice() ).toBe( 1 );
		e.editor.setChoice( 2 );
		expect( e.value() ).toBe( 2 );
		e.fixture.detectChanges();
		expect( e.fixture.nativeElement.querySelector( 'mat-select' ) ).not.toBeNull();
	} );
	//clients spec Gaps #7:  the date picker wrote the start of the day;  a DateTime takes the date and time typed, local, to the millisecond
	it( 'takes a DateTime as the date and time typed, to the millisecond, local', ()=>{
		const typed = new Date( 2026, 9, 9, 13, 4, 5, 6 );
		const e = create( {type: ETypes.DateTime, value: ProtoUtils.fromDate( new Date( 2026, 9, 9 ) )} );
		expect( e.editor.text() ).toBe( '2026-10-09T00:00:00.000' );
		e.editor.setText( DateUtils.toLocalInput( typed ) );
		expect( ProtoUtils.toDate( <Timestamp>e.value() )?.getTime() ).toBe( typed.getTime() );
		e.editor.setText( '' );
		expect( e.value() ).toBeUndefined();
	} );
	it( 'holds a String as typed, blank included, from the start', ()=>{
		const e = create( {type: ETypes.String} );
		expect( e.value() ).toBe( '' );
		e.editor.setText( 'written' );
		expect( e.value() ).toBe( 'written' );
		const xml = create( {type: ETypes.XmlElement, value: '<a/>'} );
		expect( xml.editor.kind() ).toBe( 'xml' );
		expect( xml.input( 'textarea' ).value ).toBe( '<a/>' );
	} );
	it( 'takes a Guid as 8-4-4-4-12 hex digits and nothing else', ()=>{
		const e = create( {type: ETypes.Guid, value: new Guid( '0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9' )} );
		expect( e.editor.text() ).toBe( '0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9' );
		e.editor.setText( '12345678-1234-5678-1234-567812345678' );
		expect( e.value() ).toBeInstanceOf( Guid );
		expect( e.value()!.toString() ).toBe( '12345678-1234-5678-1234-567812345678' );
		e.editor.setText( '12345678-1234-5678-1234' );
		expect( e.value() ).toBeUndefined();
		expect( e.editor.invalid() ).toContain( 'hex digits' );
	} );
	it( 'takes a ByteString as hex, the spelling a read gives, and shows one held either way', ()=>{
		const e = create( {type: ETypes.ByteString, value: new Uint8Array( [10, 11, 255] )} );
		expect( e.editor.text() ).toBe( '0a0bff' );
		e.editor.setText( '010203' );
		expect( e.value() ).toEqual( new Uint8Array( [1, 2, 3] ) );
		e.editor.setText( '0a0' );
		expect( e.value() ).toBeUndefined();
		e.editor.setText( 'zz' );
		expect( e.value() ).toBeUndefined();
		e.editor.setText( '' );
		expect( e.value() ).toEqual( new Uint8Array( [] ) );
		expect( create( {type: ETypes.ByteString, value: '0a0b'} ).editor.text() ).toBe( '0a0b' );//REST's hex
	} );
	it( 'takes a NodeId and an ExpandedNodeId in the standard\'s spelling', ()=>{
		const e = create( {type: ETypes.NodeId, value: {ns: 2, i: 5005}} );
		expect( e.editor.text() ).toBe( 'ns=2;i=5005' );
		e.editor.setText( 'ns=3;s=tag' );
		expect( e.value() ).toBeInstanceOf( NodeId );
		expect( (<NodeId>e.value()).toJson() ).toEqual( {ns: 3, s: 'tag'} );
		e.editor.setText( 'nonsense' );
		expect( e.value() ).toBeUndefined();
		expect( e.editor.invalid() ).toContain( 'ns=2;i=5' );
		const x = create( {type: ETypes.ExpandedNodeId, value: {nsu: 'urn:jde:pumps', i: 5005}} );
		expect( x.editor.text() ).toBe( 'nsu=urn:jde:pumps;i=5005' );
		x.editor.setText( 'svr=1;nsu=urn:x;ns=2;i=7' );
		const ex = <ExNodeId>x.value();
		expect( ex ).toBeInstanceOf( ExNodeId );
		expect( [ex.serverIndex, ex.nsu, ex.ns, ex.id] ).toEqual( [1, 'urn:x', 2, 7] );
		x.editor.setText( 'ns=2;i=7' );
		expect( (<ExNodeId>x.value()).nsu ).toBeUndefined();
	} );
	it( 'takes a StatusCode as hex with 0x or decimal, and shows one held as hex', ()=>{
		const e = create( {type: ETypes.StatusCode, value: 0x40920000} );
		expect( e.editor.text() ).toBe( '0x40920000' );
		e.editor.setText( '0x00960000' );
		expect( e.value() ).toBe( 0x00960000 );
		e.editor.setText( '1073741824' );
		expect( e.value() ).toBe( 0x40000000 );
		e.editor.setText( 'Good' );
		expect( e.value() ).toBeUndefined();
		expect( create( {type: ETypes.StatusCode, value: new OpcError( 0x80340000, '', '', undefined )} ).editor.text() ).toBe( '0x80340000' );
	} );
	it( 'takes a QualifiedName as its namespace index and name, and shows a push\'s text', ()=>{
		const e = create( {type: ETypes.QualifiedName, value: {ns: 2, name: 'Speed'}} );
		expect( [e.editor.text2(), e.editor.text()] ).toEqual( ['2', 'Speed'] );
		e.editor.setText( 'Rpm' );
		expect( e.value() ).toEqual( {ns: 2, name: 'Rpm'} );
		e.editor.setText2( '' );
		expect( e.value() ).toEqual( {ns: 0, name: 'Rpm'} );
		e.editor.setText2( 'x' );
		expect( e.value() ).toBeUndefined();
		expect( create( {type: ETypes.QualifiedName, value: '3:Level'} ).editor.text2() ).toBe( '3' );
	} );
	//Open questions #11:  written as its text, which keeps the locale the value had, or {text, locale} naming one
	it( 'takes a LocalizedText as its text, with a locale where one is named', ()=>{
		const e = create( {type: ETypes.LocalizedText, value: 'Pump'} );
		expect( e.editor.text() ).toBe( 'Pump' );
		e.editor.setText( 'Pumpe' );
		expect( e.value() ).toBe( 'Pumpe' );
		e.editor.setText2( 'de' );
		expect( e.value() ).toEqual( {locale: 'de', text: 'Pumpe'} );
		expect( create( {type: ETypes.LocalizedText, value: {locale: 'en', text: 'Pump'}} ).editor.text2() ).toBe( 'en' );
	} );
	it( 'edits an array as a list of its elements, added and removed, and holds nothing while one is no value', ()=>{
		const e = create( {type: ETypes.Int32, isArray: true, value: [1, 2, 3]} );
		expect( e.editor.kind() ).toBe( 'array' );
		expect( e.children().map( c=>c.kind() ) ).toEqual( ['number', 'number', 'number'] );
		e.children()[1].setText( '20' );
		expect( e.value() ).toEqual( [1, 20, 3] );
		e.editor.addElement();
		expect( e.editor.elements() ).toEqual( [1, 20, 3, 3] );//a copy of the last
		e.editor.removeElement( 0 );
		expect( e.value() ).toEqual( [20, 3, 3] );
		e.fixture.detectChanges();
		e.children()[0].setText( 'x' );
		expect( e.value() ).toBeUndefined();
		const fixed = create( {type: ETypes.Int32, isArray: true, fixedLength: true, value: [1]} );
		expect( fixed.fixture.nativeElement.querySelector( 'button.add' ) ).toBeNull();
	} );
	//clients spec *Structures*:  a structure is written member by member, each as its own value says, a nested one a group inside
	it( 'edits a structure member by member, each as its value says, nested where it is', ()=>{
		const e = create( {value: {Rpm: 1480.5, Running: true, Label: 'sample', Limits: {Low: 0, High: 3000}, History: [1, 2], Note: null}} );
		expect( e.editor.kind() ).toBe( 'structure' );
		const member = ( name:string )=>e.children().find( c=>c.label()==name )!;//the editors nest, so by the member's name, not in order
		const names = ['Rpm', 'Running', 'Label', 'Limits', 'History', 'Note'];
		expect( e.editor.members().map( m=>m.name ) ).toEqual( names );
		expect( names.map( n=>member( n ).kind() ) ).toEqual( ['number', 'boolean', 'text', 'structure', 'array', 'json'] );
		member( 'Rpm' ).setText( '1500' );
		member( 'Running' ).setFlag( false );
		expect( e.value() ).toEqual( {Rpm: 1500, Running: false, Label: 'sample', Limits: {Low: 0, High: 3000}, History: [1, 2], Note: null} );
		member( 'Low' ).setText( '5' );
		expect( (<any>e.value()).Limits ).toEqual( {Low: 5, High: 3000} );
		member( 'Rpm' ).setText( '' );
		expect( e.value() ).toBeUndefined();
	} );
	it( 'has no editor for a body no type explains, and holds it as it is', ()=>{
		const x = new ExtensionObject( new NodeId( {ns: 2, i: 3010} ), 'AAAA' );
		const e = create( {value: x} );
		expect( e.editor.kind() ).toBe( 'undecoded' );
		expect( e.fixture.nativeElement.querySelector( '.undecoded' ).textContent ).toContain( 'undecoded ns=2;i=3010' );
		expect( e.value() ).toBe( x );
	} );
	it( 'takes json for a value of no shape, blank as nothing', ()=>{
		const e = create( {value: null} );
		expect( e.editor.kind() ).toBe( 'json' );
		e.editor.setText( '[1, "a"]' );
		expect( e.value() ).toEqual( [1, 'a'] );
		e.editor.setText( '{' );
		expect( e.value() ).toBeUndefined();
		e.editor.setText( '' );
		expect( e.value() ).toBeNull();//an absent optional member stays absent
	} );
	//the fields show the value given from outside - the row's, another node's preset - and never reload what the editor itself emitted
	it( 'reloads the fields on a value from outside, and not on its own', ()=>{
		const e = create( {type: ETypes.Double, value: 1} );
		e.editor.setText( '2.50' );
		expect( e.emitted ).toEqual( [2.5] );
		e.fixture.componentRef.setInput( 'value', 2.5 );//the parent's echo of what it was given
		e.fixture.detectChanges();
		expect( e.editor.text() ).toBe( '2.50' );
		e.fixture.componentRef.setInput( 'value', 7 );
		e.fixture.detectChanges();
		expect( e.editor.text() ).toBe( '7' );
		expect( e.value() ).toBe( 7 );
	} );
	it( 'renders the control its kind takes, labelled', ()=>{
		const number = create( {type: ETypes.Int32, label: 'Speed'} );
		expect( number.input().type ).toBe( 'number' );
		expect( number.fixture.nativeElement.querySelector( 'mat-label' ).textContent ).toBe( 'Speed' );
		expect( create( {type: ETypes.DateTime} ).input().type ).toBe( 'datetime-local' );
		expect( create( {type: ETypes.Guid} ).input().type ).toBe( 'text' );
		const disabled = create( {type: ETypes.Int32, disabled: true} );
		expect( disabled.input().disabled ).toBe( true );
	} );
} );