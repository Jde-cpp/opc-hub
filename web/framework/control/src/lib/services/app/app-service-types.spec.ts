import { resolveInstanceHost, resolveInstance } from './app-service-types';

//reviews/install-issues.md #2: the installed site was built against localhost:1967, so a browser on another machine called
//its own localhost.  The page's host is the answer for an empty host and for any loopback one - the hub's own advertisement
//- and a registry name that is neither stays as it is.
describe( 'resolveInstanceHost', ()=>{
	it( 'takes the page host for an empty host', ()=>{
		expect( resolveInstanceHost("", "hub") ).toBe( "hub" );
		expect( resolveInstanceHost(undefined, "hub") ).toBe( "hub" );
	} );
	it( 'falls back to localhost with no page', ()=>{
		expect( resolveInstanceHost("", "") ).toBe( "localhost" );
	} );
	it( 'rewrites a loopback host to the page host', ()=>{
		for( const host of ["localhost", "LOCALHOST", "127.0.0.1", "::1"] )
			expect( resolveInstanceHost(host, "hub") ).toBe( "hub" );
		expect( resolveInstanceHost("localhost", "127.0.0.1") ).toBe( "127.0.0.1" );//the README's http://127.0.0.1:8071 - Origin and Host must agree
	} );
	it( 'keeps a loopback host with no page', ()=>{
		expect( resolveInstanceHost("localhost", "") ).toBe( "localhost" );
	} );
	it( 'keeps a registry name', ()=>{
		expect( resolveInstanceHost("gateway2", "hub") ).toBe( "gateway2" );
	} );
	it( 'returns the same instance when nothing changes, a copy otherwise', ()=>{
		const kept = { host:"gateway2", port:1968 };
		expect( resolveInstance(kept, "hub") ).toBe( kept );
		expect( resolveInstance({host:"localhost", port:1967, instanceName:"hub"}, "hub") ).toEqual( {host:"hub", port:1967, instanceName:"hub"} );
	} );
} );
