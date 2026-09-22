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
