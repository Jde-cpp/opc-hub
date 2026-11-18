import { ExNodeId } from './ex-node-id';

//the gateway takes a node by the server's index:  the floor's Node link and a group's root path both go through it
//(reviews-issue/487-demo-ops.md #4)
describe( 'ExNodeId.byIndex', ()=>{
	const namespaces = [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: 'urn:jde:demo'}];
	it( "takes a URI's index from the server's namespaces", ()=>{
		expect( new ExNodeId( {nsu: 'urn:jde:demo', i: 5001} ).byIndex( namespaces )?.toJson() ).toEqual( {ns: 2, i: 5001} );
	} );
	it( 'is undefined for a URI the server lacks', ()=>{
		expect( new ExNodeId( {nsu: 'urn:elsewhere', i: 5001} ).byIndex( namespaces ) ).toBeUndefined();
	} );
	it( 'keeps an index it was given, with no namespaces', ()=>{
		expect( new ExNodeId( {ns: 3, s: 'x'} ).byIndex( [] )?.toJson() ).toEqual( {ns: 3, s: 'x'} );
	} );
} );