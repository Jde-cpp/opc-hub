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
//null - and every instance of the hub's own process with it, at the listener's port, the gateway /opcGateways names at
//localhost:1967 too.  Another process on the box, at its own port, is not the front's.
describe( 'resolveInstance with the page\'s port', ()=>{
	const front = { listener: 1967, page: 443 };
	it( 'reads the port the page was browsed by, the scheme\'s own when it names none', ()=>{
		expect( pagePort({port: '', protocol: 'https:'}) ).toBe( 443 );
		expect( pagePort({port: '', protocol: 'http:'}) ).toBe( 80 );
		expect( pagePort({port: '8071', protocol: 'http:'}) ).toBe( 8071 );
	} );
	it( 'gives it to an instance at the listener\'s port on an empty or loopback host', ()=>{
		expect( resolveInstance({host: 'localhost', port: 1967, instanceName: 'hub'}, 'demo', front) ).toEqual( {host: 'demo', port: 443, instanceName: 'hub'} );
		expect( resolveInstance({host: '', port: 1967}, 'demo', front) ).toEqual( {host: 'demo', port: 443} );
		expect( resolveInstance({host: '127.0.0.1', port: 1967}, '127.0.0.1', {listener: 1967, page: 8071}) ).toEqual( {host: '127.0.0.1', port: 8071} );//the page at a loopback name itself
	} );
	it( 'leaves another process on the box its own port', ()=>{
		expect( resolveInstance({host: 'localhost', port: 1970}, 'demo', front) ).toEqual( {host: 'demo', port: 1970} );
		expect( resolveInstance({host: 'localhost', port: 1968}, 'demo', front) ).toEqual( {host: 'demo', port: 1968} );
	} );
	it( 'leaves another host\'s instance its own, at the listener\'s port too', ()=>{
		const split = { host: 'gateway2', port: 1968 };
		expect( resolveInstance(split, 'demo', front) ).toBe( split );
		const other = { host: 'gateway2', port: 1967 };
		expect( resolveInstance(other, 'demo', front) ).toBe( other );
	} );
	it( 'keeps every port with no front', ()=>{
		expect( resolveInstance({host: 'localhost', port: 1967}, 'demo', undefined) ).toEqual( {host: 'demo', port: 1967} );
	} );
} );