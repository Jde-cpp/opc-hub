import { nodeSegmentName } from './node-segment-name';

describe( 'nodeSegmentName', ()=>{
	const url = ( path:string )=>path.split( '/' ).filter( s=>s.length );

	it( 'names a node by its browse name, without the namespace or any title-casing', ()=>{
		const segments = url( '/gateways/OpcHub.debug/local/5~pump1/motorRpm' );
		expect( [3, 4].map(i=>nodeSegmentName(segments, i)) ).toEqual( ['pump1', 'motorRpm'] );
	});

	it( 'leaves the gateway, the connection and every other section to the breadcrumbs', ()=>{
		const segments = url( '/gateways/OpcHub.debug/local/5~pump1' );
		expect( [0, 1, 2].map(i=>nodeSegmentName(segments, i)) ).toEqual( [undefined, undefined, undefined] );
		expect( nodeSegmentName(url('/access/roles/5~x/y'), 3) ).toBeUndefined();
	});

	it( 'decodes an escaped name', ()=>{
		expect( nodeSegmentName(url('/gateways/g/c/2~Device%20Set'), 3) ).toBe( 'Device Set' );
	});
});
