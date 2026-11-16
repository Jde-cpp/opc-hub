import { pagePort, resolveInstanceHost, resolveInstance } from './app-service-types';

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


//#484:  behind a front the hub answers on the page's port, whatever its listener's - config.json's applicationServer.port
//null - and every instance of the hub's own process with it, the gateway /opcGateways names at localhost:1967 too.
describe( 'resolveInstance with the page\'s port', ()=>{
	it( 'reads the port the page was browsed by, the scheme\'s own when it names none', ()=>{
		expect( pagePort({port: '', protocol: 'https:'}) ).toBe( 443 );
		expect( pagePort({port: '', protocol: 'http:'}) ).toBe( 80 );
		expect( pagePort({port: '8071', protocol: 'http:'}) ).toBe( 8071 );
	} );
	it( 'gives it to an instance on an empty or loopback host', ()=>{
		expect( resolveInstance({host: 'localhost', port: 1967, instanceName: 'hub'}, 'demo', 443) ).toEqual( {host: 'demo', port: 443, instanceName: 'hub'} );
		expect( resolveInstance({host: '', port: null as any}, 'demo', 443) ).toEqual( {host: 'demo', port: 443} );
		expect( resolveInstance({host: '127.0.0.1', port: 1967}, '127.0.0.1', 8071) ).toEqual( {host: '127.0.0.1', port: 8071} );//the page at a loopback name itself
	} );
	it( 'leaves another host\'s instance its own', ()=>{
		const split = { host: 'gateway2', port: 1968 };
		expect( resolveInstance(split, 'demo', 443) ).toBe( split );
	} );
	it( 'keeps every port with none given', ()=>{
		expect( resolveInstance({host: 'localhost', port: 1967}, 'demo') ).toEqual( {host: 'demo', port: 1967} );
	} );
} );