import { MutationType } from 'jde-framework';
import { ServerCnnctn, ServerCnnctnProps } from './server-cnnctn';

//defaultBrowseNs was rendered as an editable field but neither compared nor sent - an edit silently never saved.
const props = ( overrides:Partial<ServerCnnctnProps>={} )=>({
	id: 7, slug: "local", name: "Local", url: "opc.tcp://127.0.0.1:4840", certificateUri: "urn:x", defaultBrowseNs: 1, server: undefined as any,
	...overrides
} as ServerCnnctnProps);

describe( 'ServerCnnctn.defaultBrowseNs', ()=>{
	it( 'is sent when changed', ()=>{
		const original = new ServerCnnctn( props() );
		const edited = new ServerCnnctn( props({defaultBrowseNs: 2}) );
		const [mutation] = edited.mutation( original );
		expect( mutation.args ).toEqual( {defaultBrowseNs: 2} );
		expect( mutation.type ).toBe( MutationType.Update );
	} );

	it( 'coerces the form input string to a number and compares through it', ()=>{
		const original = new ServerCnnctn( props() );
		const edited = new ServerCnnctn( props({defaultBrowseNs: "2" as any}) );
		expect( edited.defaultBrowseNs ).toBe( 2 );
		expect( edited.equals(original) ).toBe( false );
		const [mutation] = edited.mutation( original );
		expect( mutation.args ).toEqual( {defaultBrowseNs: 2} );
		expect( mutation.toString() ).toContain( 'defaultBrowseNs:2' );
	} );

	it( 'treats a raw unchanged string on the edited copy as equal', ()=>{//Properties.onChange assigns without reconstructing
		const original = new ServerCnnctn( props() );
		const edited = new ServerCnnctn( props() );
		(edited as any).defaultBrowseNs = "1";
		expect( edited.equals(original) ).toBe( true );
		expect( edited.mutation(original) ).toEqual( [] );
	} );

	//reviews/m3-closing.md #3:  a create left at the default the form shows sent nothing, so the column stayed NULL - and the
	//gateway read NULL as 0 where this form said 1.  A create states the value, so a connection never depends on what NULL means.
	it( 'is sent on a create left at the default', ()=>{
		const original = new ServerCnnctn( {} as ServerCnnctnProps );//DetailPage's row for $new
		const created = new ServerCnnctn( props({id: undefined as any, defaultBrowseNs: undefined}) );
		const [mutation] = created.mutation( original );
		expect( mutation.type ).toBe( MutationType.Create );
		expect( mutation.args.defaultBrowseNs ).toBe( 1 );
		expect( mutation.toString() ).toContain( 'defaultBrowseNs:1' );
	} );

	it( 'is sent as a number on a create whose field was typed', ()=>{//Properties.onChange assigns the input's string without reconstructing
		const original = new ServerCnnctn( {} as ServerCnnctnProps );
		const created = new ServerCnnctn( props({id: undefined as any}) );
		(created as any).defaultBrowseNs = "3";
		expect( created.mutation(original)[0].args.defaultBrowseNs ).toBe( 3 );
	} );

	it( 'falls back to the default when cleared or non-numeric', ()=>{
		expect( new ServerCnnctn(props({defaultBrowseNs: "" as any})).defaultBrowseNs ).toBe( 1 );
		expect( new ServerCnnctn(props({defaultBrowseNs: "abc" as any})).defaultBrowseNs ).toBe( 1 );
		expect( new ServerCnnctn(props({defaultBrowseNs: undefined})).defaultBrowseNs ).toBe( 1 );
	} );
} );

//url is non-null in the gateway meta; the base canSave only knows name/slug, so Save would have lit up for a connection the insert then rejected.
describe( 'ServerCnnctn.canSave', ()=>{
	it( 'needs a url as well as name and slug', ()=>{
		expect( new ServerCnnctn(props()).canSave ).toBe( true );
		expect( new ServerCnnctn(props({url: ""})).canSave ).toBe( false );
		expect( new ServerCnnctn(props({url: undefined as any})).canSave ).toBe( false );
		expect( new ServerCnnctn(props({name: ""})).canSave ).toBe( false );
	} );
} );

//reviews/install-issues.md #48:  " eng-test" was saved as-is - the provider, the certificate's file name and the `slug\user` login
//all carried the space.  Trimmed on the way out; what is left must be the gateway's rule, or the form says so and will not save.
describe( 'ServerCnnctn.slug', ()=>{
	const create = ( slug:string )=>new ServerCnnctn( props({id: undefined as any, slug}) );
	it( 'is trimmed on a create', ()=>{
		const [mutation] = create( " eng-test " ).mutation( new ServerCnnctn({} as ServerCnnctnProps) );
		expect( mutation.args.slug ).toBe( "eng-test" );
	} );
	it( 'accepts letters, digits, dot, underscore and dash', ()=>{
		for( const slug of ["local", "eng-test", "Line_1.a", " padded ", "Watch", "watch1"] ){
			expect( create(slug).fieldError("slug") ).toBeUndefined();
			expect( create(slug).canSave ).toBe( true );
		}
	} );
	it( 'refuses anything else, and will not save', ()=>{
		for( const slug of ["eng test", "eng\\test", "-eng", ".eng", "ëng", "a/b", "watch", " watch ", "templates", "groups", "calendars", "machines", "lines", "reasons", "oee"] ){//the lists' url segments (419-watch #12, 404 #1, #409), and the dashboard's (#406)
			expect( create(slug).fieldError("slug") ).toBeDefined();
			expect( create(slug).canSave ).toBe( false );
		}
	} );
	it( 'says nothing while empty, and will not save a blank one', ()=>{
		expect( create("").fieldError("slug") ).toBeUndefined();//the required asterisk covers it
		expect( create("   ").canSave ).toBe( false );
	} );
	it( 'leaves other fields alone', ()=>{
		expect( create("eng test").fieldError("name") ).toBeUndefined();
	} );
} );

//#250 review #4:  Test Connection trimmed the url and certificateUri and the save sent them as typed, so `urn:plc ` passed the test
//and was saved with its space - which the gateway's endpoint filter reads as urn:plc%20, matching nothing.  The save sends what the test tried.
describe( 'ServerCnnctn.url and certificateUri', ()=>{
	it( 'are trimmed on the way out', ()=>{
		const [mutation] = new ServerCnnctn( props({url: " opc.tcp://plc:4840 ", certificateUri: "urn:plc "}) ).mutation( new ServerCnnctn(props()) );
		expect( mutation.args ).toEqual( {url: "opc.tcp://plc:4840", certificateUri: "urn:plc"} );
	} );
	it( 'are unchanged when only whitespace was added', ()=>{
		const original = new ServerCnnctn( props() );
		const edited = new ServerCnnctn( props({url: original.url + " ", certificateUri: " " + original.certificateUri}) );
		expect( edited.equals(original) ).toBe( true );
		expect( edited.mutation(original) ).toEqual( [] );
	} );
	it( 'will not save a blank url', ()=>{
		expect( new ServerCnnctn(props({url: "   "})).canSave ).toBe( false );
	} );
} );

//docs/clients/spec.md *Connection settings* (#413):  the mode, policy, four durations and locales, each null for the gateway's own
//rule or setting.  The form writes the pick's value or the number input's string onto the record;  the mutation sends what the
//gateway stores - a trimmed string or a whole number of milliseconds, null to clear - and a create leaves an unset one out.
describe( 'ServerCnnctn connection settings', ()=>{
	const blank = ()=>new ServerCnnctn( {} as ServerCnnctnProps );
	it( 'default to null', ()=>{
		const row = new ServerCnnctn( props() );
		expect( [row.securityMode, row.securityPolicy, row.sessionTimeout, row.requestTimeout, row.pingInterval, row.ttl, row.locale] ).toEqual( [null, null, null, null, null, null, null] );
		expect( row.effectiveMode ).toBe( "SignAndEncrypt" );//the floor a certificate URI sets
		expect( new ServerCnnctn(props({certificateUri: ""})).effectiveMode ).toBe( "None" );
	} );
	it( 'are left out of a create when unset and sent when set', ()=>{
		const created = new ServerCnnctn( props({id: undefined as any, securityMode: "Sign", sessionTimeout: 60000}) );
		const [mutation] = created.mutation( blank() );
		expect( mutation.args.securityMode ).toBe( "Sign" );
		expect( mutation.args.sessionTimeout ).toBe( 60000 );
		expect( "securityPolicy" in mutation.args ).toBe( false );
		expect( "ttl" in mutation.args ).toBe( false );
	} );
	it( 'send a change and null to clear a saved value', ()=>{
		const original = new ServerCnnctn( props({securityMode: "Sign", pingInterval: 15000}) );
		const edited = new ServerCnnctn( props({securityMode: "Sign", pingInterval: null, locale: "en-US,de"}) );
		expect( edited.equals(original) ).toBe( false );
		const [mutation] = edited.mutation( original );
		expect( mutation.args ).toEqual( {pingInterval: null, locale: "en-US,de"} );
	} );
	it( 'read the number input through toMs:  a blank is unset, a string a whole number of milliseconds', ()=>{
		const original = new ServerCnnctn( props() );
		const edited = new ServerCnnctn( props() );
		(edited as any).sessionTimeout = "";
		(edited as any).ttl = "120000";
		expect( edited.equals(original) ).toBe( false );
		expect( edited.mutation(original)[0].args ).toEqual( {ttl: 120000} );
		expect( ServerCnnctn.toMs("abc") ).toBeNull();
		expect( ServerCnnctn.toMs(-5) ).toBeNull();
		expect( ServerCnnctn.toMs("1500.7") ).toBe( 1500 );
	} );
	it( 'treat whitespace and an empty pick as no change', ()=>{
		const original = new ServerCnnctn( props({locale: "en-US"}) );
		const edited = new ServerCnnctn( props({locale: " en-US ", securityMode: ""}) );
		expect( edited.equals(original) ).toBe( true );
		expect( edited.mutation(original) ).toEqual( [] );
	} );
	//the gateway's save rules (ServerCnnctn::SettingsError), said under the field so Save does not have to fail to say them
	it( 'need a certificate URI for Sign, Sign & Encrypt and a secured policy', ()=>{
		for( const mode of ["Sign", "SignAndEncrypt"] ){
			const row = new ServerCnnctn( props({certificateUri: "", securityMode: mode}) );
			expect( row.fieldError("securityMode") ).toContain( "Certificate URI" );
			expect( row.canSave ).toBe( false );
			expect( new ServerCnnctn(props({securityMode: mode})).fieldError("securityMode") ).toBeUndefined();
		}
		const policy = new ServerCnnctn( props({certificateUri: "", securityPolicy: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256"}) );
		expect( policy.fieldError("securityPolicy") ).toContain( "Certificate URI" );
		expect( policy.canSave ).toBe( false );
	} );
	it( 'refuse a policy of None on a secured channel and a secured policy on mode None', ()=>{
		const none = "http://opcfoundation.org/UA/SecurityPolicy#None";
		expect( new ServerCnnctn(props({securityPolicy: none})).fieldError("securityPolicy") ).toContain( "Certificate URI means Sign & Encrypt" );
		expect( new ServerCnnctn(props({securityPolicy: none, securityMode: "Sign"})).fieldError("securityPolicy") ).toBe( "Policy None takes mode None." );
		expect( new ServerCnnctn(props({securityPolicy: none, securityMode: "None"})).fieldError("securityPolicy") ).toBeUndefined();
		expect( new ServerCnnctn(props({certificateUri: "", securityPolicy: none})).fieldError("securityPolicy") ).toBeUndefined();
		const secured = new ServerCnnctn( props({securityMode: "None", securityPolicy: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256"}) );
		expect( secured.fieldError("securityMode") ).toContain( "Sign or Sign & Encrypt" );
		expect( secured.canSave ).toBe( false );
	} );
	//0 is the default only to the client library, which the help does not give, and a TTL of 0 closes the session after each request
	it( 'refuse 0 for every duration but the ping interval, and more than 24 days for any', ()=>{
		for( const name of ["sessionTimeout", "requestTimeout", "ttl"] ){
			const row = new ServerCnnctn( props({[name]: "0"}) );
			expect( row.fieldError(name) ).toContain( "leave it empty" );
			expect( row.canSave ).toBe( false );
		}
		expect( new ServerCnnctn(props({pingInterval: 0})).fieldError("pingInterval") ).toBeUndefined();
		const long = new ServerCnnctn( props({ttl: 2147483648}) );
		expect( long.fieldError("ttl") ).toContain( "24 days" );
		expect( long.canSave ).toBe( false );
		expect( new ServerCnnctn(props({ttl: 2147483647})).fieldError("ttl") ).toBeUndefined();
	} );
	it( 'answer testConnection\'s arguments, null where unset', ()=>{
		expect( ServerCnnctn.settings(new ServerCnnctn(props({securityMode: "Sign", requestTimeout: "2000" as any}))) ).toEqual( {securityMode: "Sign", securityPolicy: null, sessionTimeout: null, requestTimeout: 2000, pingInterval: null, ttl: null, locale: null} );
	} );
} );

//docs/open-protocol/spec.md *The row* (#475):  `kind` and `driver` on the row, null for a UA row, and the Open Protocol form's fields -
//host, port, time zone, ceilings - which the row reads off the two columns and composes them from.  A kind whose server is the
//gateway's own stores none of the four the gateway fixes, and the mutation sends none of them for it.
describe( 'ServerCnnctn kind and driver', ()=>{
	const blank = ()=>new ServerCnnctn( {} as ServerCnnctnProps );
	const op = ( overrides:Partial<ServerCnnctnProps>={} )=>new ServerCnnctn( props({kind: "open-protocol", url: "open-protocol://plc:4546", certificateUri: undefined as any, defaultBrowseNs: undefined, driver: '{"timeZone":"UTC","revisions":{"0061":3}}', ...overrides}) );
	it( 'reads a null kind as ua, and the Open Protocol fields off the columns', ()=>{
		expect( new ServerCnnctn(props()).kind ).toBe( "ua" );
		expect( new ServerCnnctn(props({kind: null})).kind ).toBe( "ua" );
		expect( new ServerCnnctn(props({kind: " ua "})).isOpenProtocol ).toBe( false );
		const row = op();
		expect( row.isOpenProtocol ).toBe( true );
		expect( [row.host, row.port, row.timeZone, row.revisions] ).toEqual( ["plc", "4546", "UTC", "0061:3"] );
		expect( row.effectiveUrl ).toBe( "open-protocol://plc:4546" );
		expect( row.effectiveDriver ).toBe( '{"timeZone":"UTC","revisions":{"0061":3}}' );
		const ua = new ServerCnnctn( props() );
		expect( [ua.host, ua.port, ua.timeZone, ua.revisions] ).toEqual( ["", "4545", "", ""] );//a row switched to the kind starts blank
		expect( ua.effectiveDriver ).toBeNull();
	} );
	it( 'keeps the form fields on an edited copy, and composes the url and the driver from them', ()=>{
		const edited = new ServerCnnctn( {...op(), host: " station-2 ", port: "", timeZone: "Europe/Stockholm", revisions: "0001:6 0061:3"} as ServerCnnctnProps );
		expect( edited.effectiveUrl ).toBe( "open-protocol://station-2:4545" );
		expect( edited.effectiveDriver ).toBe( '{"timeZone":"Europe/Stockholm","revisions":{"0001":6,"0061":3}}' );
		expect( edited.url ).toBe( "open-protocol://plc:4546" );//the column as stored, until the save
	} );
	it( 'states the kind on a create, and sends none of the four a driver kind fixes', ()=>{
		const created = new ServerCnnctn( props({id: undefined as any, kind: "open-protocol", url: "", certificateUri: "urn:x", securityMode: "Sign", host: "plc", port: "", timeZone: "UTC", revisions: "0061:3"}) );
		expect( created.canSave ).toBe( true );
		const [mutation] = created.mutation( blank() );
		expect( mutation.type ).toBe( MutationType.Create );
		expect( mutation.args.kind ).toBe( "open-protocol" );
		expect( mutation.args.url ).toBe( "open-protocol://plc:4545" );
		expect( mutation.args.driver ).toBe( '{"timeZone":"UTC","revisions":{"0061":3}}' );
		for( const fixed of ["certificateUri", "defaultBrowseNs", "securityMode", "securityPolicy"] )
			expect( fixed in mutation.args ).toBe( false );
		const ua = new ServerCnnctn( props({id: undefined as any}) ).mutation( blank() )[0];
		expect( ua.args.kind ).toBe( "ua" );
		expect( ua.args.defaultBrowseNs ).toBe( 1 );
		expect( "driver" in ua.args ).toBe( false );
	} );
	it( 'sends a change of host, port, zone or ceilings as the url and the driver, and nothing for whitespace', ()=>{
		const original = op();
		expect( new ServerCnnctn({...original, host: "plc ", revisions: " 0061:3"} as ServerCnnctnProps).mutation( original ) ).toEqual( [] );
		const moved = new ServerCnnctn( {...original, host: "plc2", port: "4547"} as ServerCnnctnProps );
		expect( moved.equals(original) ).toBe( false );
		expect( moved.mutation(original)[0].args ).toEqual( {url: "open-protocol://plc2:4547"} );
		const retimed = new ServerCnnctn( {...original, timeZone: "", revisions: ""} as ServerCnnctnProps );
		expect( retimed.mutation(original)[0].args ).toEqual( {driver: null} );
		const handWritten = new ServerCnnctn( props({kind: "open-protocol", url: "open-protocol://plc:4546", driver: '{"revisions": {"0061": 3}, "timeZone": "UTC"}'}) );
		expect( new ServerCnnctn({...handWritten} as ServerCnnctnProps).mutation( handWritten ) ).toEqual( [] );//compared in the canonical form, not as text
	} );
	it( 'changes a UA row to Open Protocol with the kind and the composed url alone', ()=>{
		const original = new ServerCnnctn( props({securityMode: "Sign", sessionTimeout: 60000}) );
		const edited = new ServerCnnctn( {...original, kind: "open-protocol", host: "plc", port: "4545"} as ServerCnnctnProps );
		expect( edited.canSave ).toBe( true );//the Sign left on the record from its UA days says nothing
		expect( edited.fieldError("securityMode") ).toBeUndefined();
		expect( edited.mutation(original)[0].args ).toEqual( {kind: "open-protocol", url: "open-protocol://plc:4545"} );
		expect( ServerCnnctn.settings(edited).securityMode ).toBeNull();
		expect( ServerCnnctn.settings(edited).sessionTimeout ).toBe( 60000 );
	} );
	it( 'changes an Open Protocol row back to UA with the url it needs, a null driver and the namespace stated again', ()=>{
		const original = op();
		const edited = new ServerCnnctn( {...original, kind: "ua", url: "opc.tcp://plc:4840", certificateUri: ""} as ServerCnnctnProps );
		expect( edited.canSave ).toBe( true );
		expect( edited.mutation(original)[0].args ).toEqual( {kind: "ua", url: "opc.tcp://plc:4840", driver: null, certificateUri: "", defaultBrowseNs: 1} );
	} );
	it( 'will not save an Open Protocol row with no host or a field the gateway would refuse', ()=>{
		expect( new ServerCnnctn({...op(), host: ""} as ServerCnnctnProps).canSave ).toBe( false );
		expect( new ServerCnnctn({...op(), host: "plc 1"} as ServerCnnctnProps).canSave ).toBe( false );
		expect( new ServerCnnctn({...op(), port: "70000"} as ServerCnnctnProps).canSave ).toBe( false );
		expect( new ServerCnnctn({...op(), timeZone: "Mars/Olympus"} as ServerCnnctnProps).canSave ).toBe( false );
		expect( new ServerCnnctn({...op(), revisions: "61:3"} as ServerCnnctnProps).canSave ).toBe( false );
		expect( op().canSave ).toBe( true );
	} );
	it( 'refuses an Open Protocol url on a UA row, under the url', ()=>{
		const row = new ServerCnnctn( props({url: "open-protocol://plc:4545"}) );
		expect( row.fieldError("url") ).toContain( "Open Protocol" );
		expect( row.canSave ).toBe( false );
		expect( op().fieldError("url") ).toBeUndefined();
	} );
} );