import { NodeId } from './node-id';

describe( 'node-id', ()=>{
	it( 'writes QL args for each identifier type', ()=>{
		expect( new NodeId({ns: 4, i: 5003}).qlArgs() ).toBe( 'ns:4,i:5003' );
		expect( new NodeId({ns: 2, s: 'Tank.Level'}).qlArgs() ).toBe( 'ns:2,s:"Tank.Level"' );
		expect( NodeId.qlArgsArray([new NodeId({ns: 0, i: 85}), new NodeId({ns: 1, s: 'x'})]) ).toBe( '{ns:0,i:85},{ns:1,s:"x"}' );
	} );

	//CodeQL js/incomplete-sanitization:  the string id went into the query unescaped, so a quote ended the literal early.
	it( 'escapes quotes and backslashes in a string id', ()=>{
		const id = 'a"b\\c\\"d';
		const args = new NodeId( {ns: 3, s: id} ).qlArgs();
		expect( args ).toBe( 'ns:3,s:"a\\"b\\\\c\\\\\\"d"' );
		expect( JSON.parse(args.substring('ns:3,s:'.length)) ).toBe( id );
	} );
} );
