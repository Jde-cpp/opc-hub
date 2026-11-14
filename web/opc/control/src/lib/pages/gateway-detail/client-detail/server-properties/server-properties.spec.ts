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
//#415:  the certificate panels - the rejected server certificate with Trust, and the gateway's own with a PEM download.
describe( 'ServerProperties certificate panels', ()=>{
	const rejected = { subject: "CN=plc", issuer: "CN=plc", notBefore: "2026-01-01T00:00:00Z", notAfter: "2027-01-01T00:00:00Z", uri: "urn:plc", thumbprint: "AB12CD34" };
	const client = { path: "/var/lib/Jde-Cpp/OpcHub/ssl/certs/OpcHub.plc.pem", subject: "CN=gateway", notBefore: "2026-01-01T00:00:00Z", notAfter: "2027-01-01T00:00:00Z", uri: "urn:gw", pem: "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n" };
	afterEach( ()=>vi.restoreAllMocks() );//the PEM-download test spies on URL and the anchor prototype
	const render = ( inputs:Record<string,unknown> )=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( ServerProperties );
		for( const [name, value] of Object.entries(inputs) )
			fixture.componentRef.setInput( name, value );
		fixture.detectChanges();
		return fixture;
	};

	it( 'shows the rejected certificate and emits its thumbprint on Trust', ()=>{
		const fixture = render( {error: "server certificate rejected", rejected} );
		const root = fixture.nativeElement as HTMLElement;
		expect( texts(root, '.certificate.rejected dl.fields dd span') ).toEqual( ["CN=plc", "CN=plc", "2026-01-01T00:00:00Z", "2027-01-01T00:00:00Z", "urn:plc", "AB12CD34"] );
		const trusted:string[] = [];
		fixture.componentInstance.trust.subscribe( (t)=>trusted.push(t) );
		(root.querySelector('.certificate.rejected button') as HTMLButtonElement).click();
		expect( trusted ).toEqual( ["AB12CD34"] );
	} );

	it( 'disables Trust and reads Trusting while a trust is in flight', ()=>{
		const root = render( {rejected, trusting: true} ).nativeElement as HTMLElement;
		const button = root.querySelector( '.certificate.rejected button' ) as HTMLButtonElement;
		expect( button.disabled ).toBe( true );
		expect( button.textContent!.trim() ).toBe( "Trusting…" );
	} );

	//reviews-issue/415-clients-trust.md #7:  a detached anchor, and its url revoked under the click - Firefox and Safari read the url
	//after click() returns, and saved nothing.
	it( 'downloads the gateway certificate as a .pem named for its file', ()=>{
		const created:Blob[] = [];
		vi.spyOn( URL, 'createObjectURL' ).mockImplementation( (b)=>{ created.push(b as Blob); return 'blob:x'; } );
		const revoked:string[] = [];
		vi.spyOn( URL, 'revokeObjectURL' ).mockImplementation( (u)=>{ revoked.push(u); } );
		const clicks:{ name:string; attached:boolean; anchor:HTMLAnchorElement }[] = [];
		vi.spyOn( HTMLAnchorElement.prototype, 'click' ).mockImplementation( function( this:HTMLAnchorElement ){ clicks.push( {name: this.download, attached: this.isConnected, anchor: this} ); } );
		const root = render( {client} ).nativeElement as HTMLElement;
		expect( texts(root, '.certificate.client dl.fields dd span') ).toEqual( ["CN=gateway", "urn:gw", "2026-01-01T00:00:00Z", "2027-01-01T00:00:00Z", client.path] );
		vi.useFakeTimers();
		try{
			(root.querySelector('.certificate.client button') as HTMLButtonElement).click();
			expect( created.length ).toBe( 1 );
			expect( clicks.map( ({name, attached})=>({name, attached}) ) ).toEqual( [{name: "OpcHub.plc.pem", attached: true}] );
			expect( clicks[0].anchor.isConnected ).toBe( false );//and taken out again
			expect( revoked ).toEqual( [] );//not under the click
			vi.runAllTimers();
			expect( revoked ).toEqual( ["blob:x"] );
		}
		finally{
			vi.useRealTimers();
		}
	} );

	it( 'shows neither panel when there is no certificate', ()=>{
		const root = render( {server: new Server(props())} ).nativeElement as HTMLElement;
		expect( root.querySelector('.certificate') ).toBeNull();
	} );
} );
//docs/open-protocol/spec.md *Web* (#475):  an Open Protocol row's two sections in place of a server's description - the controller a
//Test Connection reached, MID 0002's fields, and a saved row's `_Op`, its driver's diagnostics, a variable that reads a status in place
//of a value saying so in words.
describe( 'ServerProperties for an Open Protocol row', ()=>{
	const identity = { controllerName: "PF6000-1", supplierCode: "ACT", openProtocolVersion: "2.20.1", controllerSoftwareVersion: "3.8.0", toolSoftwareVersion: null, controllerSerialNumber: "SN1", systemType: 3, systemSubtype: 1, stationId: 2, stationName: "Station A" };
	const op = {
		Connected: {value: true}, Since: {value: {seconds: 1760000000, nanos: 0}}, Url: {value: "open-protocol://plc:4545"}, LastError: {value: ""}, Reconnects: {value: 2},
		Identity: {value: JSON.stringify(identity)}, Revisions: {value: "0001:6 0061:7"}, LinkLevel: {value: false}, Results: {value: {low: 5, high: 1, unsigned: true}}, LostResults: {value: 0},
		Skew: {sc: 0x80320000}
	};
	const render = ( inputs:Record<string,unknown> )=>{
		TestBed.resetTestingModule();//a test may render twice
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( ServerProperties );
		for( const [name, value] of Object.entries(inputs) )
			fixture.componentRef.setInput( name, value );
		fixture.detectChanges();
		return fixture.nativeElement as HTMLElement;
	};
	const rows = ( root:HTMLElement, section:string )=>Array.from( root.querySelectorAll(`section.${section} dl.fields dt`) ).map( (dt, i)=>[dt.textContent!.trim(), root.querySelectorAll(`section.${section} dl.fields dd`)[i].textContent!.trim()] );

	it( 'shows the controller a test reached in place of a description, a blank field marked', ()=>{
		const root = render( {identity} );
		expect( root.querySelector('section.identity h3')!.textContent ).toBe( "Controller" );
		expect( rows(root, "identity") ).toEqual( [
			["Controller", "PF6000-1"], ["System", "Power Focus 6000, subtype 1"], ["Supplier Code", "ACT"], ["Open Protocol Version", "2.20.1"], ["Controller Software", "3.8.0"],
			["Tool Software", "none"], ["Serial Number", "SN1"], ["Station", "Station A (2)"]
		] );
		expect( root.querySelector('.not-connected') ).toBeNull();
		expect( root.querySelector('.untested') ).toBeNull();
	} );
	it( 'shows a saved row\'s _Op in place of the driver server\'s description, the identity spelled out and a status in words', ()=>{
		const root = render( {server: new Server(props()), op, openProtocol: true} );
		expect( root.querySelector('table.list') ).toBeNull();//no discovery urls, no namespaces:  the driver server's say nothing of the controller
		expect( root.querySelector('section.op h3')!.textContent ).toBe( "Driver" );
		const shown = rows( root, "op" );
		expect( shown.map( ([label])=>label ) ).toEqual( [
			"Connected", "Since", "Last Error", "Reconnects", "URL", "Controller", "System", "Supplier Code", "Open Protocol Version", "Controller Software", "Tool Software", "Serial Number", "Station",
			"Link Level", "Revisions", "Clock Skew", "Results", "Lost Results"
		] );
		const value = ( label:string )=>shown.find( ([l])=>l==label )![1];
		expect( value("Connected") ).toBe( "yes" );
		expect( value("Link Level") ).toBe( "no" );
		expect( value("Since") ).not.toBe( "none" );
		expect( value("Last Error") ).toBe( "none" );
		expect( value("Reconnects") ).toBe( "2" );
		expect( value("Controller") ).toBe( "PF6000-1" );
		expect( value("Results") ).toBe( String(5 + 2**32) );//a UInt64 in the Long form
		expect( value("Lost Results") ).toBe( "0" );
		expect( value("Clock Skew") ).toBe( "waiting for the first read" );
		expect( root.querySelector('section.op .status')!.textContent ).toBe( "waiting for the first read" );
	} );
	it( 'says what a variable reads where the controller gives none, and that a row was not answered', ()=>{
		const root = render( {server: new Server(props()), openProtocol: true, op: {...op, Skew: {sc: 0x803D0000}, Connected: {value: false}, Identity: {value: ""}, Results: undefined}} );
		const shown = rows( root, "op" );
		const value = ( label:string )=>shown.find( ([l])=>l==label )![1];
		expect( value("Clock Skew") ).toBe( "not supported by the controller" );
		expect( value("Connected") ).toBe( "no" );
		expect( value("Controller") ).toBe( "none" );//no identity yet:  one row for it
		expect( shown.filter( ([l])=>l=="System" ) ).toEqual( [] );
		expect( value("Results") ).toBe( "not answered" );
	} );
	it( 'says the diagnostics are being read, or why they could not be', ()=>{
		expect( render({server: new Server(props()), openProtocol: true, opReading: true}).querySelector('section.op p.op-reading')!.textContent ).toContain( "Reading" );
		expect( render({server: new Server(props()), openProtocol: true, opError: "(403) no session"}).querySelector('section.op p.op-error')!.textContent ).toBe( "(403) no session" );
		expect( render({server: new Server(props()), openProtocol: true}).querySelector('dl.fields') ).not.toBeNull();//a UA row's description, until a read is under way:  the form's kind alone decides nothing
	} );
	it( 'shows the identity above a saved row\'s _Op after a test', ()=>{
		const root = render( {server: new Server(props()), identity, op, openProtocol: true} );
		const sections = Array.from( root.querySelectorAll('section') ).map( s=>s.className );
		expect( sections ).toEqual( ["identity", "op"] );
	} );
	it( 'tells a new Open Protocol connection what the test asks', ()=>{
		const root = render( {untested: true, openProtocol: true} );
		expect( root.querySelector('.untested .detail')!.textContent ).toContain( "asks the controller" );
		expect( render({untested: true}).querySelector('.untested .detail')!.textContent ).toContain( "URL and Certificate URI" );
	} );
} );