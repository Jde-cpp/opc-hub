import Long from 'long';
import { DateUtils } from 'jde-framework';
import { Argument, argumentValue, MethodArguments, toCallResult } from './method';
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
		expect( arg( {dataType: {ns: 2, i: 3002}} ).editor ).toBe( 'number' );//an enumeration goes as the number it is encoded as
		expect( arg( {dataType: {i: ETypes.DateTime}} ).editor ).toBe( 'datetime' );
		expect( arg( {dataType: {i: ETypes.String}} ).editor ).toBe( 'text' );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ).editor ).toBe( 'json' );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: 0} ).isArray ).toBe( true );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: -3} ).isArray ).toBe( false );
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
	} );
} );