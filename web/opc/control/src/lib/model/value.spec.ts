import Long from 'long';
import { Guid } from 'jde-framework';
import { bytesOfHex, ExtensionObject, hexOf, isStructure, parseGuid, shortestFloat, toReading, toValue, valueJson, valueString } from './value';
import { ExNodeId } from './ex-node-id';
import { NodeId } from './node-id';
import { OpcError } from './opc-error';

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
		expect( valueString(<any>{ns: 2, i: 5}) ).toBe( 'ns=2;i=5' );//REST's NodeId, as a push's NodeId spells it
		expect( valueString(new NodeId({ns: 2, i: 5})) ).toBe( 'ns=2;i=5' );
		expect( valueString(<any>{ns: 2, name: "Speed"}) ).toBe( '2:Speed' );
		//reviews-issue/423-clients-writes.md #6:  a push's ExpandedNodeId and ByteString read as REST's do - the uri kept, the bytes hex
		expect( valueString(<any>{nsu: 'urn:jde:pumps', i: 5005}) ).toBe( 'nsu=urn:jde:pumps;i=5005' );
		expect( valueString(new ExNodeId({ns: 0, i: 5005, nsu: 'urn:jde:pumps'})) ).toBe( 'nsu=urn:jde:pumps;i=5005' );
		expect( valueString(<any>{ns: 3, s: 'a', serverindex: 2}) ).toBe( 'svr=2;ns=3;s=a' );
		expect( valueString(new Uint8Array([1, 2, 3])) ).toBe( '010203' );
		expect( valueString(<any>{}) ).toBe( '' );
	} );
	//clients plan 8b (#422):  a body no type explains - a pre-1.04 server's, say - says so, never its bytes
	it( 'says an ExtensionObject is undecoded, with its type id and not its bytes', ()=>{
		const typeId = new NodeId( {ns: 2, i: 5001} );
		expect( valueString(new ExtensionObject(typeId, new Uint8Array([1, 2, 3]))) ).toBe( "undecoded ns=2;i=5001" );
		expect( valueString(new ExtensionObject(typeId, "<Range/>")) ).toBe( "undecoded ns=2;i=5001" );
		expect( valueString(new ExtensionObject(typeId)) ).toBe( "undecoded ns=2;i=5001" );
		expect( new ExtensionObject(typeId).tooltip ).toContain( "ns=2;i=5001" );
	} );
	//the #421 walk's W1:  a structure decoded by member read "unknown type object"
	it( 'shows a structure as its members, a 64-bit one exact', ()=>{
		const big = Long.fromString( "9007199254740993" );
		const structure = { Rpm: 1480.5, Limits: {Low: 0, High: 3000}, Count: {low: big.low, high: big.high, unsigned: false}, Total: big };
		expect( valueString(<any>structure) ).toBe( '{"Rpm":1480.5,"Limits":{"Low":0,"High":3000},"Count":"9007199254740993","Total":"9007199254740993"}' );
	} );
} );

//clients plan 8b (#422):  a structure decoded by member is a plain object of them, the one shape off the socket and off the REST json,
//and the plain objects that are not structures - a Timestamp, the Long form, a NodeId, a QualifiedName, an ExpandedNodeId, an opaque
//body, {} - are told apart by their whole shape (reviews-issue/422-structures.md #6)
describe( 'isStructure', ()=>{
	it( 'is a plain object of members, and not a Timestamp, a Long form, a NodeId or a QualifiedName', ()=>{
		expect( isStructure({Rpm: 1480.5, Limits: {Low: 0, High: 3000}}) ).toBe( true );
		expect( isStructure({}) ).toBe( false );//an empty QualifiedName, or a union with no member selected:  nothing to tabulate
		expect( isStructure({ns: 2, i: 5}) ).toBe( false );
		expect( isStructure({s: "Pump.Speed"}) ).toBe( false );
		expect( isStructure({ns: 2, name: "Speed"}) ).toBe( false );
		expect( isStructure({nsu: "urn:x", i: 5}) ).toBe( false );
		expect( isStructure({ns: 2, i: 5, Rpm: 1}) ).toBe( true );//a structure with those member names and another
		expect( isStructure({ns: 2, i: 5, s: "x"}) ).toBe( true );//two identifiers:  no NodeId
		expect( isStructure({seconds: 1762300000, nanos: 0}) ).toBe( false );
		expect( isStructure({low: 1, high: 0, unsigned: false}) ).toBe( false );
		expect( isStructure({seconds: 1, nanos: 0, Extra: 2}) ).toBe( true );//a structure with those member names and another
		expect( isStructure([{Rpm: 1}]) ).toBe( false );
		expect( isStructure(new NodeId({ns: 2, i: 1})) ).toBe( false );
		expect( isStructure(new ExtensionObject(new NodeId({ns: 2, i: 1}))) ).toBe( false );
		expect( isStructure(null) ).toBe( false );
		expect( isStructure(3) ).toBe( false );
	} );
	it( 'reads the gateway\'s opaque form, {typeId, body}, as an ExtensionObject', ()=>{
		const x = toValue( {typeId: {ns: 2, i: 777}, body: "AAAA"} ) as ExtensionObject;
		expect( x ).toBeInstanceOf( ExtensionObject );
		expect( x.typeId.uaString() ).toBe( "ns=2;i=777" );
		expect( x.body ).toBe( "AAAA" );
		expect( toValue({typeId: {ns: 2, i: 777}}) ).toBeInstanceOf( ExtensionObject );//no body
		expect( isStructure(toValue({typeId: {ns: 2, i: 777}, body: "AAAA", Rpm: 1})) ).toBe( true );//a structure with those member names stays one
		expect( toReading({typeId: {ns: 2, i: 777}, body: "AAAA"}).value ).toBeInstanceOf( ExtensionObject );
		expect( isStructure(toValue({typeId: 7})) ).toBe( true );//a member named typeId that holds no NodeId
	} );
	//#7, #8:  REST's structure becomes the socket's shape member by member, and a member named as the reading's wrapper is a member
	it( 'converts a REST structure\'s members as the socket\'s are', ()=>{
		const s = <any>toValue( {Count: {low: 1, high: 2097152, unsigned: false}, Inner: {typeId: {ns: 2, i: 3010}, body: "AAAA"}, Limits: {Low: 0, High: {low: 5, high: 0, unsigned: true}}, Ids: [{ns: 2, i: 5}], Note: null} );
		expect( s.Count ).toBeInstanceOf( Long );
		expect( s.Count.toString() ).toBe( "9007199254740993" );
		expect( s.Inner ).toBeInstanceOf( ExtensionObject );
		expect( s.Limits.High ).toBeInstanceOf( Long );
		expect( s.Ids ).toEqual( [{ns: 2, i: 5}] );
		expect( s.Note ).toBeNull();
		expect( valueString(s) ).toBe( '{"Count":"9007199254740993","Inner":"undecoded ns=2;i=3010","Limits":{"Low":0,"High":"5"},"Ids":[{"ns":2,"i":5}],"Note":null}' );//the text a view sorts and filters by:  never the body
		const r = toReading( {sc: 7, Rpm: 1} );
		expect( r.sc ).toBe( 0 );//a Good structure with an `sc` member:  not a Bad reading
		expect( (<any>r.value).sc ).toBe( 7 );
		expect( (<any>toValue({v: 3, Other: 1})).v ).toBe( 3 );//nor a {v,sc}
		expect( toValue({v: 3, sc: 1073741824}) ).toBe( 3 );//the wrapper, by its whole key set
		expect( toReading({sc: 2147483648}).value ).toBeUndefined();
	} );
} );

//#195 review #3:  valueJson is the write side, and fell through to the instance - a NodeId with methods and a Uint8Array body.
describe( 'valueJson', ()=>{
	it( 'refuses an ExtensionObject rather than serialising the class', ()=>{
		const x = new ExtensionObject( new NodeId({ns: 2, i: 5001}), new Uint8Array([1, 2]) );
		expect( ()=>valueJson(x) ).toThrow( /ns=2;i=5001/ );
		expect( ()=>valueJson([1, x]) ).toThrow();
	} );
	//clients spec *Values and writes* (#423):  each built-in type as Value::Set reads it - a NodeId whole, with its namespace, where it
	//went as the identifier alone;  an ExpandedNodeId with its uri and server index, the gateway's spelling;  a Guid its text;  a
	//ByteString {b: base64};  a structure its members, each this way
	it( 'spells each kind as the gateway reads it', ()=>{
		expect( valueJson(new NodeId({ns: 2, s: 'tag'})) ).toEqual( {ns: 2, s: 'tag'} );
		expect( valueJson(new ExNodeId({ns: 0, i: 7, nsu: 'urn:x', serverIndex: 2})) ).toEqual( {ns: 0, i: 7, nsu: 'urn:x', serverindex: 2} );
		expect( valueJson(new ExNodeId({ns: 2, i: 7})) ).toEqual( {ns: 2, i: 7} );
		expect( valueJson(new Guid('0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9')) ).toBe( '0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9' );
		expect( valueJson(new Uint8Array([1, 2, 3])) ).toEqual( {b: 'AQID'} );
		expect( valueJson(new OpcError(0x80340000, '', '', undefined)) ).toEqual( {sc: 0x80340000} );
		expect( valueJson({Rpm: 1, Id: new NodeId({ns: 2, i: 5}), Note: null, Ids: [new NodeId({i: 1})]}) ).toEqual( {Rpm: 1, Id: {ns: 2, i: 5}, Note: null, Ids: [{ns: 0, i: 1}]} );
		expect( valueJson({ns: 2, name: 'Speed'}) ).toEqual( {ns: 2, name: 'Speed'} );//a QualifiedName, as it is
	} );
} );

//the ByteString and Guid editors' text (clients spec *Values and writes*, #423)
describe( 'hexOf, bytesOfHex and parseGuid', ()=>{
	it( 'spell bytes as hex and read them back, refusing an odd count or a non-digit', ()=>{
		expect( hexOf(new Uint8Array([10, 11, 255])) ).toBe( '0a0bff' );
		expect( bytesOfHex(' 0A0bFF ') ).toEqual( new Uint8Array([10, 11, 255]) );
		expect( bytesOfHex('') ).toEqual( new Uint8Array([]) );
		expect( bytesOfHex('0a0') ).toBeUndefined();
		expect( bytesOfHex('zz') ).toBeUndefined();
	} );
	it( 'reads a Guid as 8-4-4-4-12 hex digits', ()=>{
		expect( parseGuid(' 0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9 ')?.toString() ).toBe( '0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9' );
		expect( parseGuid('0a1b2c3d-4e5f-6071-8293') ).toBeUndefined();
		expect( parseGuid('0a1b2c3d4e5f60718293a4b5c6d7e8f9') ).toBeUndefined();
	} );
} );