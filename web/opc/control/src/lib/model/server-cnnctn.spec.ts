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
		for( const slug of ["local", "eng-test", "Line_1.a", " padded "] ){
			expect( create(slug).fieldError("slug") ).toBeUndefined();
			expect( create(slug).canSave ).toBe( true );
		}
	} );
	it( 'refuses anything else, and will not save', ()=>{
		for( const slug of ["eng test", "eng\\test", "-eng", ".eng", "ëng", "a/b"] ){
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