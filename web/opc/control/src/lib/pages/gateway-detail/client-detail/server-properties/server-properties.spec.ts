import { TestBed } from '@angular/core/testing';
import { Server, ServerProps } from '../../../../model/server';
import { ServerProperties } from './server-properties';

//Was eight disabled form fields:  dimmed, unselectable (so the URIs could not be copied out), a bare centred label where
//the server left a field blank, "Uri" against the sibling tab's "URI", and the discovery URLs commented out altogether.
//the Props types carry the row's audit columns and a getter the constructors never read - `as any` on the two nested literals
const props = ( overrides:Partial<ServerProps>={} ):ServerProps=>({
	connection: { id: 7, slug: "local", name: "Local", url: "opc.tcp://127.0.0.1:4840", certificateUri: "urn:x", defaultBrowseNs: 1 } as any,
	desc: {
		applicationName: "Jde-Cpp OpcServer [debug]", applicationUri: "urn:open62541.server.application", applicationType: "Server",
		productUri: "http://open62541.org", gatewayServerUri: "", discoveryProfileUri: "",
		discoveryUrls: ["opc.tcp://127.0.0.1:4840", "opc.tcp://[::1]:4840"]
	} as any,
	policy: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", mode: "SignAndEncrypt",
	namespaces: [ {index: 0, uri: "http://opcfoundation.org/UA/"}, {index: 5, uri: "urn:jde:pumps"} ],
	...overrides
});
const create = ( server:Server|undefined, error?:string )=>{
	TestBed.configureTestingModule({});
	const fixture = TestBed.createComponent( ServerProperties );
	fixture.componentRef.setInput( 'server', server );
	fixture.componentRef.setInput( 'error', error );
	fixture.detectChanges();
	return fixture.nativeElement as HTMLElement;
};
const texts = ( root:HTMLElement, selector:string )=>Array.from( root.querySelectorAll(selector) ).map( (e)=>e.textContent!.trim() );

describe( 'ServerProperties', ()=>{
	it( 'renders the description as label/value rows, not inputs', ()=>{
		const root = create( new Server(props()) );
		expect( root.querySelector('input') ).toBeNull();
		expect( texts(root, 'dl.fields dt') ).toEqual( [
			"Application Name", "Application URI", "Application Type", "Product URI", "Policy", "Mode", "Gateway Server URI", "Discovery Profile URI"
		] );
		expect( texts(root, 'dl.fields dd')[1] ).toBe( "urn:open62541.server.application" );
		expect( texts(root, 'dl.fields dd')[0] ).toBe( "Jde-Cpp OpcServer [debug]" );
	} );

	//the policy uri's prefix is the same for every policy and the mode's wire name runs its words together
	it( 'abbreviates the policy and mode, keeping the full value in the title', ()=>{
		const root = create( new Server(props()) );
		const shown = Array.from( root.querySelectorAll('dl.fields dd span') ) as HTMLElement[];
		expect( shown[4].textContent ).toBe( "Basic256Sha256" );
		expect( shown[4].title ).toBe( "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" );
		expect( shown[5].textContent ).toBe( "Sign & Encrypt" );
		expect( shown[5].title ).toBe( "SignAndEncrypt" );
		expect( shown[0].hasAttribute('title') ).toBe( false );//an unabbreviated value has no tooltip
	} );

	it( 'shows a policy without a fragment and an unlisted mode as they came', ()=>{
		const root = create( new Server(props({policy: "urn:custom:policy", mode: "Sign"})) );
		const shown = Array.from( root.querySelectorAll('dl.fields dd span') ) as HTMLElement[];
		expect( shown[4].textContent ).toBe( "urn:custom:policy" );
		expect( shown[4].hasAttribute('title') ).toBe( false );
		expect( shown[5].textContent ).toBe( "Sign" );
		expect( shown[5].hasAttribute('title') ).toBe( false );
	} );

	it( 'marks a blank field rather than dropping the row or leaving it empty', ()=>{
		const root = create( new Server(props()) );
		const values = texts( root, 'dl.fields dd' );
		expect( values[6] ).toBe( "none" );
		expect( values[7] ).toBe( "none" );
		expect( root.querySelectorAll('dl.fields dd .empty').length ).toBe( 2 );
	} );

	it( 'lists each discovery url on its own row', ()=>{
		const root = create( new Server(props()) );
		const [urls] = Array.from( root.querySelectorAll('table.list') ).filter( (t)=>t.querySelector('caption')!.textContent=="Discovery URLs" );
		expect( texts(urls as HTMLElement, 'td') ).toEqual( ["opc.tcp://127.0.0.1:4840", "opc.tcp://[::1]:4840"] );
	} );

	it( 'shows an empty state when the server reported no discovery urls', ()=>{
		const root = create( new Server(props({desc: {...props().desc, discoveryUrls: undefined as any}})) );
		const [urls] = Array.from( root.querySelectorAll('table.list') ).filter( (t)=>t.querySelector('caption')!.textContent=="Discovery URLs" );
		expect( texts(urls as HTMLElement, 'td.empty') ).toEqual( ["None reported."] );
	} );

	it( 'keeps the namespace table in the server\'s order with its index column', ()=>{
		const root = create( new Server(props()) );
		expect( texts(root, 'table.namespaces .ns-index') ).toEqual( ["0", "5"] );
		expect( texts(root, 'table.namespaces .ns-uri') ).toEqual( ["http://opcfoundation.org/UA/", "urn:jde:pumps"] );
	} );
} );

//The tab used to be hidden when the gateway had no session with the server, which told the user nothing about why.
describe( 'ServerProperties without a server', ()=>{
	it( 'says it is not connected and why, in place of the description', ()=>{
		const root = create( undefined, "(403)BadIdentityTokenRejected - Connection Failed" );
		expect( root.querySelector('dl.fields') ).toBeNull();
		expect( root.querySelector('table.list') ).toBeNull();
		expect( texts(root, '.not-connected .title') ).toEqual( ["Not connected"] );
		expect( texts(root, '.not-connected .detail') ).toEqual( ["(403)BadIdentityTokenRejected - Connection Failed"] );
	} );

	it( 'falls back to a generic reason when none was recorded', ()=>{
		const root = create( undefined );
		expect( texts(root, '.not-connected .detail') ).toEqual( ["The gateway has no session with this server."] );
	} );

	it( 'is no live region:  inserted with its text it may go unannounced, so ClientDetail announces (#250 review #15)', ()=>{
		expect( create(undefined, "no route to host").querySelector('[role=status]') ).toBeNull();
	} );

	//#250 review L1:  a new connection never tried was Not connected under the error icon, and read as a failure.
	it( 'says a new connection is not tested yet, without the error state', ()=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( ServerProperties );
		fixture.componentRef.setInput( 'untested', true );
		fixture.detectChanges();
		const root = fixture.nativeElement as HTMLElement;
		expect( root.querySelector('.not-connected') ).toBeNull();
		expect( texts(root, '.untested .title') ).toEqual( ["Not tested yet"] );
		expect( texts(root, '.untested .detail')[0] ).toContain( "Test Connection checks" );
	} );

	it( 'shows no not-connected state when there is a server', ()=>{
		expect( create(new Server(props())).querySelector('.not-connected') ).toBeNull();
	} );
} );
//#250 review #9:  a passing test of one url stayed under a form edited to another, and read as a check of the new one.
describe( 'ServerProperties for settings the form no longer holds', ()=>{
	const render = ( inputs:Record<string,unknown> )=>{
		TestBed.resetTestingModule();//a test may render twice
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( ServerProperties );
		for( const [name, value] of Object.entries(inputs) )
			fixture.componentRef.setInput( name, value );
		fixture.detectChanges();
		return fixture.nativeElement as HTMLElement;
	};
	it( 'says what the result was for, above it, and keeps the result', ()=>{
		const root = render( {server: new Server(props()), staleFor: "URL opc.tcp://a:4840"} );
		expect( texts(root, '.stale .title') ).toEqual( ["The form has changed"] );
		expect( texts(root, '.stale .detail')[0] ).toContain( "This result was for URL opc.tcp://a:4840." );
		const fields = root.querySelector( 'dl.fields' )!;
		expect( root.querySelector('.stale')!.compareDocumentPosition(fields) & Node.DOCUMENT_POSITION_FOLLOWING ).toBeTruthy();
	} );
	it( 'marks a failure too', ()=>{
		const root = render( {error: "no route to host", staleFor: "URL opc.tcp://a:4840"} );
		expect( root.querySelector('.stale') ).not.toBeNull();
		expect( texts(root, '.not-connected .detail') ).toEqual( ["no route to host"] );
	} );
	it( 'is not shown while a test connects, or when the form matches', ()=>{
		expect( render({connecting: "opc.tcp://b:4840", staleFor: "URL opc.tcp://a:4840"}).querySelector('.stale') ).toBeNull();
		expect( render({server: new Server(props())}).querySelector('.stale') ).toBeNull();
	} );
} );