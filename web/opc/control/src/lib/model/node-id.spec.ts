import { NodeId } from './node-id';

describe( 'node-id', ()=>{
	it( 'writes QL args for each identifier type', ()=>{
		expect( new NodeId({ns: 4, i: 5003}).qlArgs() ).toBe( 'ns:4,i:5003' );
		expect( new NodeId({ns: 2, s: 'Tank.Level'}).qlArgs() ).toBe( 'ns:2,s:"Tank.Level"' );
	} );

	//CodeQL js/incomplete-sanitization:  the string id went into the query unescaped, so a quote ended the literal early.
	it( 'escapes quotes and backslashes in a string id', ()=>{
		const id = 'a"b\\c\\"d';
		const args = new NodeId( {ns: 3, s: id} ).qlArgs();
		expect( args ).toBe( 'ns:3,s:"a\\"b\\\\c\\\\\\"d"' );
		expect( JSON.parse(args.substring('ns:3,s:'.length)) ).toBe( id );
	} );
} );

describe( 'NodeId.fromUaString', ()=>{
	it( 'round-trips uaString for every identifier kind', ()=>{
		for( const s of ["ns=5;i=5005", "i=85", "ns=2;s=pump 1", "ns=3;g=12345678-1234-1234-1234-123456789abc"] )
			expect( NodeId.fromUaString(s).uaString() ).toBe( s );
	} );
	it( 'refuses anything else', ()=>{
		expect( ()=>NodeId.fromUaString("pump1") ).toThrow( /not a NodeId/ );
	} );
} );