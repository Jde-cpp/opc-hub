import Long from 'long';
import { DateUtils } from 'jde-framework';
import { Enum } from './enum';
import { Argument, argumentValue, MethodArguments, toCallResult } from './method';
import { NodeId } from './node-id';
import { ETypes } from './types';

const arg = ( json:object )=>new Argument( <any>{ name: 'a', ...json } );

//clients spec *Methods* (#318):  `method( opc, id )` answers each argument as the server declares it, and the dialog's editor
//follows its type as the Children tab's cells do.
describe( 'Argument', ()=>{
	it( 'reads a namespace-0 data type as one of ETypes, and another namespace as the server\'s own', ()=>{
		const rpm = arg( {dataType: {ns: 0, i: 11}, valueRank: -1, description: 'rpm'} );
		expect( rpm.dataType ).toBe( ETypes.Double );
		expect( rpm.customDataType ).toBeUndefined();
		expect( rpm.typeName ).toBe( 'Double' );
		expect( rpm.isArray ).toBe( false );
		const custom = arg( {dataType: {ns: 2, i: 3002}} );
		expect( custom.dataType ).toBe( ETypes.None );
		expect( custom.customDataType?.uaString() ).toBe( 'ns=2;i=3002' );
		expect( custom.typeName ).toBe( 'ns=2;i=3002' );
	} );
	it( 'picks the editor by type:  a toggle, a number box, a date, text, and json for an array', ()=>{
		expect( arg( {dataType: {i: ETypes.Boolean}} ).editor ).toBe( 'boolean' );
		expect( arg( {dataType: {i: ETypes.Int32}} ).editor ).toBe( 'number' );
		expect( arg( {dataType: {i: ETypes.Duration}} ).editor ).toBe( 'number' );
		expect( arg( {dataType: {i: ETypes.DateTime}} ).editor ).toBe( 'datetime' );
		expect( arg( {dataType: {i: ETypes.String}} ).editor ).toBe( 'text' );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ).editor ).toBe( 'json' );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: 0} ).isArray ).toBe( true );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: -3} ).isArray ).toBe( false );
	} );
} );

//reviews-issue/318-opc-calls.md #3:  a type of the server's own goes as the namespace-0 type it derives from, which `method` answers
//as baseDataType - the editor follows that, and an enumeration's is a choice of its values.
describe( 'An argument of the server\'s own type', ()=>{
	const health = new Enum( new NodeId( <any>{ns: 2, i: 6244} ), {name: 'DeviceHealthEnumeration', enumValues: [{id: 0, name: 'NORMAL'}, {id: 2, name: 'OFF_SPEC'}]} as any );
	const enumeration = ( withValues:boolean )=>{
		const a = arg( {dataType: {ns: 2, i: 6244}, baseDataType: {i: ETypes.Enumeration}} );
		if( withValues )
			a.enum = health;
		return a;
	};
	it( 'takes its base\'s editor and value type', ()=>{
		const speed = arg( {dataType: {ns: 5, s: 'Speed'}, baseDataType: {i: ETypes.Double}} );
		expect( speed.valueType ).toBe( ETypes.Double );
		expect( speed.editor ).toBe( 'number' );
		expect( speed.typeName ).toBe( 'ns=5;s=Speed, a Double' );
		expect( argumentValue( speed, '1500', false ) ).toBe( 1500 );
		expect( argumentValue( arg( {dataType: {ns: 5, i: 1}, baseDataType: {i: ETypes.Int64}} ), '9007199254740993', false ) ).toEqual( Long.fromString( '9007199254740993' ) );
		expect( arg( {dataType: {ns: 5, i: 1}, baseDataType: {i: ETypes.String}} ).editor ).toBe( 'text' );
		expect( arg( {dataType: {ns: 5, i: 1}, baseDataType: {i: ETypes.Boolean}} ).editor ).toBe( 'boolean' );
	} );
	it( 'is json where the server names no base', ()=>{
		const unknown = arg( {dataType: {ns: 2, i: 3002}, baseDataType: null} );
		expect( unknown.baseType ).toBeUndefined();
		expect( unknown.editor ).toBe( 'json' );
		expect( unknown.typeName ).toBe( 'ns=2;i=3002' );
		expect( argumentValue( unknown, '3', false ) ).toBe( 3 );
	} );
	it( 'is a choice of an enumeration\'s values, by name, sent as the id', ()=>{
		const a = enumeration( true );
		expect( a.isEnumeration ).toBe( true );
		expect( a.editor ).toBe( 'enum' );
		expect( a.typeName ).toBe( 'DeviceHealthEnumeration' );
		expect( argumentValue( a, '2', false ) ).toBe( 2 );
		expect( argumentValue( a, '', false ) ).toBeUndefined();
		expect( a.valueText( 2 ) ).toBe( 'OFF_SPEC' );
		expect( a.valueText( 7 ) ).toBe( '7' );//one the server didn't name
		expect( a.valueText( [0, 2] ) ).toBe( '[NORMAL, OFF_SPEC]' );
	} );
	it( 'is the number an enumeration is sent as, where its values could not be read', ()=>{
		const a = enumeration( false );
		expect( a.editor ).toBe( 'number' );
		expect( argumentValue( a, '2', false ) ).toBe( 2 );
	} );
} );

describe( 'argumentValue', ()=>{
	it( 'types the text as the editor has it, and has no value for text that is none of its type', ()=>{
		expect( argumentValue( arg( {dataType: {i: ETypes.Boolean}} ), '', true ) ).toBe( true );
		expect( argumentValue( arg( {dataType: {i: ETypes.Double}} ), '1500', false ) ).toBe( 1500 );
		expect( argumentValue( arg( {dataType: {i: ETypes.Double}} ), 'fast', false ) ).toBeUndefined();
		expect( argumentValue( arg( {dataType: {i: ETypes.Int64}} ), '9007199254740993', false ) ).toEqual( Long.fromString( '9007199254740993' ) );
		expect( argumentValue( arg( {dataType: {i: ETypes.String}} ), 'fast', false ) ).toBe( 'fast' );
		expect( argumentValue( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ), '[1, 2]', false ) ).toEqual( [1, 2] );
		expect( argumentValue( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ), '[1, ', false ) ).toBeUndefined();
		expect( argumentValue( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ), '  ', false ) ).toBeUndefined();
		const when = new Date( 2026, 10, 4, 10, 30, 0, 250 );
		expect( argumentValue( arg( {dataType: {i: ETypes.DateTime}} ), DateUtils.toLocalInput( when ), false ) ).toEqual( {seconds: Math.floor( when.getTime()/1000 ), nanos: 250_000_000} );
		expect( argumentValue( arg( {dataType: {i: ETypes.DateTime}} ), '', false ) ).toBeUndefined();
	} );
} );

describe( 'MethodArguments and toCallResult', ()=>{
	it( 'reads the gateway\'s shapes', ()=>{
		const m = new MethodArguments( {name: 'toHertz', inputArguments: [{name: 'rpm', dataType: {i: 11}, valueRank: -1, description: 'The motor speed, in rpm.'}], outputArguments: [{name: 'hertz', dataType: {i: 11}}, {name: 'text', dataType: {i: 12}}]} );
		expect( m.name ).toBe( 'toHertz' );
		expect( m.inputs.map( a=>a.name ) ).toEqual( ['rpm'] );
		expect( m.outputs.map( a=>a.typeName ) ).toEqual( ['Double', 'String'] );
		expect( new MethodArguments( {} ) ).toMatchObject( {name: '', inputs: [], outputs: []} );
		expect( toCallResult( {statusCode: 0, inputArgumentResults: [], outputArguments: [25, '25 Hz']} ) ).toEqual( {statusCode: 0, inputArgumentResults: [], outputArguments: [25, '25 Hz']} );
		expect( toCallResult( {statusCode: 0x80AB0000, inputArgumentResults: [0x80740000], outputArguments: []} ) ).toEqual( {statusCode: 0x80AB0000, inputArgumentResults: [0x80740000], outputArguments: []} );
		expect( toCallResult( {statusCode: 0, outputArguments: [{high: 0, low: 7, unsigned: false}]} ).outputArguments[0] ).toEqual( Long.fromNumber( 7 ) );
		expect( toCallResult( undefined ) ).toEqual( {statusCode: 0, inputArgumentResults: [], outputArguments: []} );
		const handles = arg( {dataType: {i: ETypes.UInt32}, valueRank: 1} );//reviews-issue/318-opc-calls.md W1:  an empty array reads as one, not as nothing
		expect( handles.valueText( [] ) ).toBe( '[]' );
		expect( handles.valueText( [1, 2] ) ).toBe( '[1, 2]' );
	} );
} );