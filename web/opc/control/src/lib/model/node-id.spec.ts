import { ExNodeId } from './ex-node-id';
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

describe( 'NodeId.equals', ()=>{
	//Gateway.toNode builds a fresh Guid or Uint8Array for every message:  compared by identity, no push matched its row (419-watch #3)
	it( 'compares a Guid or ByteString id by value', ()=>{
		const g = '12345678-1234-1234-1234-123456789abc';
		expect( new NodeId( {ns: 3, g} ).equals( new NodeId( {ns: 3, g} ) ) ).toBe( true );
		expect( new NodeId( {ns: 3, b: 'AQID'} ).equals( new NodeId( {ns: 3, b: 'AQID'} ) ) ).toBe( true );
		expect( new NodeId( {ns: 3, b: 'AQID'} ).equals( new NodeId( {ns: 3, b: 'AQIE'} ) ) ).toBe( false );
		expect( new NodeId( {ns: 3, b: 'AQID'} ).equals( new NodeId( {ns: 3, b: 'AQIDBA=='} ) ) ).toBe( false );//a prefix
		expect( new NodeId( {ns: 3, g} ).equals( new NodeId( {ns: 4, g} ) ) ).toBe( false );
		expect( new NodeId( {ns: 3, g} ).equals( new NodeId( {ns: 3, s: g} ) ) ).toBe( false );
	} );
} );

describe( 'NodeId.fromUaString', ()=>{
	it( 'round-trips uaString for every identifier kind', ()=>{
		for( const s of ["ns=5;i=5005", "i=85", "ns=2;s=pump 1", "ns=3;g=12345678-1234-1234-1234-123456789abc"] )
			expect( NodeId.fromUaString(s).uaString() ).toBe( s );
	} );
	it( 'refuses anything else', ()=>{
		expect( ()=>NodeId.fromUaString("pump1") ).toThrow( /not a NodeId/ );
		for( const s of ["i=abc", "i=", "ns=2;i=1.5", "ns=2;s="] )
			expect( ()=>NodeId.fromUaString(s) ).toThrow( /not a NodeId/ );
	} );
} );

//clients spec *Values and writes* (#423):  the ExpandedNodeId editor's spelling, the standard's, as the gateway prints one
describe( 'ExNodeId spelling', ()=>{
	it( 'writes and reads svr= and nsu= ahead of the node, each where set', ()=>{
		expect( new ExNodeId( {ns: 2, i: 7} ).uaString() ).toBe( 'ns=2;i=7' );
		expect( new ExNodeId( {ns: 0, i: 7, nsu: 'urn:x'} ).uaString() ).toBe( 'nsu=urn:x;i=7' );
		expect( new ExNodeId( {ns: 3, s: 'a', nsu: 'urn:x', serverIndex: 1} ).uaString() ).toBe( 'svr=1;nsu=urn:x;s=a' );//the index yields to the uri
		const x = ExNodeId.fromUaString( ' svr=2;nsu=urn:jde:pumps;i=5005 ' );
		expect( [x.serverIndex, x.nsu, x.ns, x.id] ).toEqual( [2, 'urn:jde:pumps', 0, 5005] );
		const plain = ExNodeId.fromUaString( 'ns=2;s=tag' );
		expect( [plain.serverIndex, plain.nsu, plain.ns, plain.id] ).toEqual( [undefined, undefined, 2, 'tag'] );
		expect( ()=>ExNodeId.fromUaString( 'nsu=urn:x;nonsense' ) ).toThrow();
		expect( new ExNodeId( {i: 7, serverindex: 3} ).serverIndex ).toBe( 3 );//the gateway's spelling of the index
	} );
} );