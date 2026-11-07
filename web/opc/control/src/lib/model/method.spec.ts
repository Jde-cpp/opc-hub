import Long from 'long';
import { Enum } from './enum';
import { Argument, MethodArguments, toCallResult } from './method';
import { NodeId } from './node-id';
import { ETypes } from './types';

const arg = ( json:object )=>new Argument( <any>{ name: 'a', ...json } );

//clients spec *Methods* (#318):  `method( opc, id )` answers each argument as the server declares it, and the dialog's editor
//(ValueEditor, the write dialog's) follows its type.
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
	it( 'hands the editor its type, and whether it is an array', ()=>{
		expect( arg( {dataType: {i: ETypes.Boolean}} ).valueType ).toBe( ETypes.Boolean );
		expect( arg( {dataType: {i: ETypes.Duration}} ).valueType ).toBe( ETypes.Duration );
		expect( arg( {dataType: {i: ETypes.Double}, valueRank: 1} ).isArray ).toBe( true );
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
	it( 'takes its base\'s value type', ()=>{
		const speed = arg( {dataType: {ns: 5, s: 'Speed'}, baseDataType: {i: ETypes.Double}} );
		expect( speed.valueType ).toBe( ETypes.Double );
		expect( speed.typeName ).toBe( 'ns=5;s=Speed, a Double' );
		expect( arg( {dataType: {ns: 5, i: 1}, baseDataType: {i: ETypes.Boolean}} ).valueType ).toBe( ETypes.Boolean );
	} );
	it( 'has no value type where the server names no base', ()=>{
		const unknown = arg( {dataType: {ns: 2, i: 3002}, baseDataType: null} );
		expect( unknown.baseType ).toBeUndefined();
		expect( unknown.valueType ).toBeUndefined();
		expect( unknown.typeName ).toBe( 'ns=2;i=3002' );
	} );
	it( 'is an enumeration, named, whose outputs read by name', ()=>{
		const a = enumeration( true );
		expect( a.isEnumeration ).toBe( true );
		expect( a.valueType ).toBe( ETypes.Enumeration );
		expect( a.typeName ).toBe( 'DeviceHealthEnumeration' );
		expect( a.valueText( 2 ) ).toBe( 'OFF_SPEC' );
		expect( a.valueText( 7 ) ).toBe( '7' );//one the server didn't name
		expect( a.valueText( [0, 2] ) ).toBe( '[NORMAL, OFF_SPEC]' );
		expect( enumeration( false ).enum ).toBeUndefined();//its values could not be read:  the editor is the number it is sent as
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