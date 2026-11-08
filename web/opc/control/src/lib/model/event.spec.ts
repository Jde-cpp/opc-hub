import { baseEventFields, EBaseField, eventColumns, eventFieldText, eventTime, selectProblem, toEventRow } from './event';
import { NodeId } from './node-id';

//An event's fields as the socket delivers them (clients spec *Events*):  the eight base fields, then the selected ones.
const fields = ( message='hello', severity=500, extra:unknown[]=[] )=>[
	Uint8Array.from( [1, 2, 0xab] ), new NodeId( {i: 2041} ), new NodeId( {ns: 2, i: 5005} ), 'Pump (Manual)',
	{seconds: 1_700_000_000, nanos: 250_000_000}, {seconds: 1_700_000_000, nanos: 300_000_000}, {text: message}, severity, ...extra
];

describe( 'toEventRow', ()=>{
	it( 'keys a row by its EventId as hex, and times it by its Time field', ()=>{
		const row = toEventRow( <any>fields() );
		expect( row.key ).toBe( '0102ab' );
		expect( row.time?.getTime() ).toBe( 1_700_000_000_250 );
		expect( row.fields ).toHaveLength( 8 );
	} );
	it( 'takes REST\'s hex EventId as the key as it is, so a history fill dedupes against the pushes', ()=>{
		const pushed = toEventRow( <any>fields() ), read = toEventRow( <any>['0102ab', ...fields().slice( 1 )] );
		expect( read.key ).toBe( pushed.key );
	} );
	it( 'gives a row without an EventId a key of its own, and the receive time where the Time is missing', ()=>{
		const received = new Date( 1_700_000_100_000 );
		const a = toEventRow( <any>[undefined, ...fields().slice( 1, 4 ), undefined], received ), b = toEventRow( <any>[undefined], received );
		expect( a.key ).not.toBe( b.key );
		expect( a.time ).toBeUndefined();
		expect( a.received ).toBe( received );
	} );
} );

describe( 'eventTime', ()=>{
	it( 'reads a Date, the {seconds, nanos} both paths carry, and nothing else', ()=>{
		expect( eventTime( <any>new Date( 5 ) )?.getTime() ).toBe( 5 );
		expect( eventTime( {seconds: 1, nanos: 500_000_000} )?.getTime() ).toBe( 1500 );
		expect( eventTime( 'now' ) ).toBeUndefined();
		expect( eventTime( {ns: 0, i: 1} ) ).toBeUndefined();
	} );
} );

describe( 'eventFieldText', ()=>{
	it( 'shows a LocalizedText by its text, a String as it is, and nothing for a field the server left empty', ()=>{
		expect( eventFieldText( {text: 'hello', locale: 'en'} ) ).toBe( 'hello' );
		expect( eventFieldText( {text: 'hello'} ) ).toBe( 'hello' );
		expect( eventFieldText( 'Pump (Manual)' ) ).toBe( 'Pump (Manual)' );
		expect( eventFieldText( 500 ) ).toBe( '500' );
		expect( eventFieldText( undefined ) ).toBe( '' );
	} );
} );

//EventClause::Parse's rules:  segments on `/`, a namespace index before `:` or `~`, a UInt16
describe( 'selectProblem', ()=>{
	it( 'takes a name, an indexed name and a path, and refuses what the gateway would', ()=>{
		expect( selectProblem( 'Rpm' ) ).toBeUndefined();
		expect( selectProblem( '2:Rpm' ) ).toBeUndefined();
		expect( selectProblem( '2~Rpm' ) ).toBeUndefined();
		expect( selectProblem( 'EnabledState/Id' ) ).toBeUndefined();
		expect( selectProblem( '' ) ).toBeDefined();
		expect( selectProblem( 'Message/' ) ).toBeDefined();
		expect( selectProblem( '70000:x' ) ).toBeDefined();
	} );
} );

describe( 'eventColumns', ()=>{
	it( 'lists the base fields but the EventId, then each selected path at its place in the field list', ()=>{
		const columns = eventColumns( ['Rpm', '2:Foo'] );
		expect( columns.map( c=>c.label ) ).toEqual( ['EventType', 'SourceNode', 'SourceName', 'Time', 'ReceiveTime', 'Message', 'Severity', 'Rpm', '2:Foo'] );
		expect( columns.find( c=>c.label=='Rpm' )?.index ).toBe( baseEventFields.length );
		expect( columns.find( c=>c.label=='Severity' )?.index ).toBe( EBaseField.Severity );
		expect( new Set( columns.map( c=>c.key ) ).size ).toBe( columns.length );
	} );
} );