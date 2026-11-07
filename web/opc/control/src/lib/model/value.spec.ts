import Long from 'long';
import { ExtensionObject, shortestFloat, toReading, valueJson, valueString } from './value';
import { NodeId } from './node-id';

//the gateway's three shapes (Value::ToJson):  the bare value for Good, {v,sc} for any other code that is not Bad, {sc} for Bad.
describe( 'toReading', ()=>{
	it( 'reads a bare value as Good', ()=>{
		expect( toReading(42.5) ).toEqual( {value: 42.5, sc: 0} );
		expect( toReading(false) ).toEqual( {value: false, sc: 0} );
		expect( toReading("abc") ).toEqual( {value: "abc", sc: 0} );
	} );

	it( 'keeps the quality beside the value', ()=>{
		expect( toReading({v: 1500, sc: 0x40940600}) ).toEqual( {value: 1500, sc: 0x40940600} );
		expect( toReading({v: 0, sc: 0x00960000}) ).toEqual( {value: 0, sc: 0x00960000} );//a falsy value is still a value
	} );

	it( 'has no value for a Bad reading - not an OpcError standing in for one', ()=>{
		const reading = toReading( {sc: 0x808C0000} );
		expect( reading ).toEqual( {sc: 0x808C0000} );
		expect( "value" in reading ).toBe( false );
	} );

	it( 'unwraps a Long and an array inside the wrapper', ()=>{
		const reading = toReading( {v: {low: 7, high: 0, unsigned: true}, sc: 0x40940000} );
		expect( reading.value ).toBeInstanceOf( Long );
		expect( (reading.value as Long).toNumber() ).toBe( 7 );
		expect( toReading({v: [1, 2], sc: 0x40940000}) ).toEqual( {value: [1, 2], sc: 0x40940000} );
		expect( toReading([1, 2]) ).toEqual( {value: [1, 2], sc: 0} );
	} );

	it( 'takes a null - the server\'s empty value - as it comes', ()=>{
		expect( toReading(null) ).toEqual( {value: null, sc: 0} );
	} );
	//reviews-issue/417-clients-attributes.md #5:  a structure value's `source` member became the reading's source time, an Invalid Date
	it( 'takes the timestamps off a DataValue alone', ()=>{
		const dv = toReading( {v: 1, sc: 0, source: {seconds: 1762300000, nanos: 0}, server: {seconds: 1762300001, nanos: 0}} );
		expect( [dv.source?.getTime(), dv.server?.getTime()] ).toEqual( [1762300000000, 1762300001000] );
		const structure = toReading( {source: 'PLC1', server: 'srv', rpm: 3} );
		expect( [structure.source, structure.server] ).toEqual( [undefined, undefined] );
		expect( structure.value ).toEqual( {source: 'PLC1', server: 'srv', rpm: 3} );
	} );
} );

//A Float pushed over the socket decoded widened to a double, 21.3 as 21.299999237060547, and every screen showed that
//(historian-web-trend, Tooltip decimals).
describe( 'shortestFloat', ()=>{
	it( 'is the fewest digits that are still the float', ()=>{
		expect( shortestFloat( Math.fround(21.3) ) ).toBe( 21.3 );
		expect( shortestFloat( Math.fround(0.000123) ) ).toBe( 0.000123 );
		expect( shortestFloat( Math.fround(16777217) ) ).toBe( 16777216 );//past 2^24 a float holds even integers alone
		expect( shortestFloat( 42 ) ).toBe( 42 );
		expect( shortestFloat( NaN ) ).toBeNaN();
	} );
	it( 'narrows back to the same float', ()=>{
		for( const x of [21.3, 0.1, 3.4028234663852886e38, 1.401298464324817e-45, -1.1754943508222875e-38, 123456.7] ){
			const f = Math.fround( x );
			expect( Math.fround(shortestFloat(f)) ).toBe( f );
		}
	} );
} );

describe( 'valueString', ()=>{
	//reviews-issue/417-clients-attributes.md W1:  a DateTime value read in UTC ISO beside its timestamps and editor in local time
	it( 'shows a DateTime in local time, as the timestamp columns do', ()=>{
		const local = new Date( 2026, 10, 5, 20, 18, 11, 92 );//the zone the spec runs in
		expect( valueString(<any>{seconds: Math.floor(local.getTime()/1000), nanos: 92_000_000}) ).toBe( '2026-11-05 20:18:11.092' );
		expect( valueString(<any>{seconds: 0, nanos: 0}) ).toBe( '' );//unset
	} );
	it( 'shows an ExtensionObject as its type id and body', ()=>{
		const typeId = new NodeId( {ns: 2, i: 5001} );
		expect( valueString(new ExtensionObject(typeId, new Uint8Array([1, 2, 3]))) ).toBe( "ns=2;i=5001 AQID" );
		expect( valueString(new ExtensionObject(typeId, "<Range/>")) ).toBe( "ns=2;i=5001 <Range/>" );
		expect( valueString(new ExtensionObject(typeId)) ).toBe( "ns=2;i=5001" );
	} );
	//the #421 walk's W1:  a structure decoded by member read "unknown type object"
	it( 'shows a structure as its members, a 64-bit one exact', ()=>{
		const big = Long.fromString( "9007199254740993" );
		const structure = { Rpm: 1480.5, Limits: {Low: 0, High: 3000}, Count: {low: big.low, high: big.high, unsigned: false}, Total: big };
		expect( valueString(<any>structure) ).toBe( '{"Rpm":1480.5,"Limits":{"Low":0,"High":3000},"Count":"9007199254740993","Total":"9007199254740993"}' );
	} );
} );

//#195 review #3:  valueJson is the write side, and fell through to the instance - a NodeId with methods and a Uint8Array body.
describe( 'valueJson', ()=>{
	it( 'refuses an ExtensionObject rather than serialising the class', ()=>{
		const x = new ExtensionObject( new NodeId({ns: 2, i: 5001}), new Uint8Array([1, 2]) );
		expect( ()=>valueJson(x) ).toThrow( /ns=2;i=5001/ );
		expect( ()=>valueJson([1, x]) ).toThrow();
	} );
} );