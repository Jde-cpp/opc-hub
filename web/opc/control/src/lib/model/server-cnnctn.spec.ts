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
