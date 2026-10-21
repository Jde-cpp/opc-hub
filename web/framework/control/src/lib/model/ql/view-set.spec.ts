import { LocalPrefs, ProfileStore } from 'jde-spa';
import { View, ViewType } from './view';
import { ViewSet } from './view-set';
import { id, required, scalar, schemaOf } from 'jde-framework/testing';

//reviews/angular-refactor.md A1:  QLList, LogDetail and NodeChildren each kept their own copy of these rules, and each had fixed
//a different subset of the same bugs.
const schema = schemaOf( "Thing", id(), required("name"), scalar("kind") );
class ThingView extends View{}//a host's subclass - LogView, NodeView

describe( 'ViewSet', ()=>{
	const make = ( name:string|undefined, type=ViewType.System, sort="name" )=>{
		const view = ThingView.fromConfig( {name, configColumns: ["name", "kind"], sort: [{active: sort, direction: "asc"}]}, schema );
		view.type = type;
		return view;
	};
	//saved:  the User views the profile row holds.  persisted:  the names the last save wrote.
	const create = ( views?:ThingView[], index=0, saved:unknown[] = [] )=>{
		const save = vi.fn().mockResolvedValue( undefined );
		const profileStore = {
			loadArray: async ( _key:string, revive:( json:any )=>unknown )=>saved.map( revive ),
			save
		} as unknown as ProfileStore;
		const set = new ViewSet( {type: ThingView, key: "things", profileStore, prefs: new LocalPrefs()} );
		if( views )
			set.reset( views, index );
		return { set, save, persisted: ()=>( save.mock.lastCall![1] as {name:string}[] ).map( v=>v.name ) };
	};
	const names = ( set:ViewSet<ThingView> )=>set.views().map( v=>`${v.name ?? "Default"}:${ViewType[v.type]}` );
	beforeEach( ()=>localStorage.clear() );

	it( 'loads the system views, then the saved ones as its type, on the remembered view', async ()=>{
		localStorage.setItem( "things/viewIndex", "1" );
		const { set } = create( undefined, 0, [make( "Mine", ViewType.User ).toJson( undefined )] );
		await set.load( [make( undefined )], schema );
		expect( names(set) ).toEqual( ["Default:System", "Mine:User"] );
		expect( set.current() ).toBeInstanceOf( ThingView );
		expect( set.current().name ).toBe( "Mine" );
	} );

	it( 'clamps a remembered index past the views', async ()=>{
		localStorage.setItem( "things/viewIndex", "5" );
		const { set } = create();
		await set.load( [make( undefined ), make( "Users" )], schema );
		expect( set.index() ).toBe( 1 );
	} );

	describe( 'show', ()=>{
		it( 'makes an Adhoc twin named as its view, and replaces it on the next show', ()=>{
			const { set } = create( [make( undefined ), make( "Users" )], 1 );
			const shown = View.copy( set.current() );
			set.show( shown );
			expect( names(set) ).toEqual( ["Default:System", "Users:System", "Users:Adhoc"] );
			expect( set.current() ).toBeInstanceOf( ThingView );//ql-list-settings emits a base View
			expect( shown.type ).toBe( ViewType.System );//the caller's copy is left alone
			set.show( set.current() );
			expect( names(set) ).toEqual( ["Default:System", "Users:System", "Users:Adhoc"] );
		} );

		it( "drops the '*' the settings panel shows on an unnamed twin", ()=>{
			const { set } = create( [make( undefined ), make( "x*", ViewType.Adhoc )], 1 );
			set.show( set.current() );
			expect( set.current().name ).toBe( "x" );
		} );

		it( 'leaves the remembered index where the twin came from', ()=>{
			const { set } = create( [make( undefined ), make( "Users" )] );
			set.select( 1 );
			set.show( set.current() );
			expect( set.index() ).toBe( 2 );
			expect( localStorage.getItem("things/viewIndex") ).toBe( "1" );//the twin is never saved, so after a reload its index points elsewhere
		} );
	} );

	it( 'sorts on a twin, the column first and the rest kept', ()=>{
		const { set } = create( [make( undefined )] );
		set.current().sort = [{active: "name", direction: "asc"}, {active: "kind", direction: "asc"}];
		set.sort( {active: "kind", direction: "desc"} );
		expect( set.current().isAdhoc ).toBe( true );
		expect( set.current().sort ).toEqual( [{active: "kind", direction: "desc"}, {active: "name", direction: "asc"}] );
		expect( set.views()[0].sort[0].active ).toBe( "name" );//the view it came from is untouched
	} );

	describe( 'select', ()=>{
		it( 'discards the twin on switching away, and finds the index after it', ()=>{
			const { set } = create( [make( undefined ), make( "Users" )] );
			set.show( set.current() );
			expect( set.select( 1 ) ).toBe( true );
			expect( names(set) ).toEqual( ["Default:System", "Users:System"] );
			expect( set.current().name ).toBe( "Users" );
			expect( localStorage.getItem("things/viewIndex") ).toBe( "1" );
		} );

		it( 'keeps the twin when it is the one selected', ()=>{
			const { set } = create( [make( undefined ), make( "Users" )] );
			set.show( set.current() );
			set.select( 1 );
			set.show( set.current() );
			set.select( 2 );
			expect( names(set) ).toEqual( ["Default:System", "Users:System", "Users:Adhoc"] );
		} );

		it( 'does nothing for an index that is not there', ()=>{
			const { set } = create( [make( undefined )] );
			expect( set.select( 3 ) ).toBe( false );
			expect( set.current().name ).toBeUndefined();
		} );
	} );

	describe( 'save', ()=>{
		it( 'promotes a twin to User, drops the twin and persists the User views', async ()=>{
			const { set, save, persisted } = create( [make( undefined ), make( "Mine", ViewType.User )], 1 );
			set.show( set.current() );
			await set.save( set.current() );//QLList left an edited twin Adhoc, so Save on it persisted nothing
			expect( names(set) ).toEqual( ["Default:System", "Mine:User"] );
			expect( set.index() ).toBe( 1 );
			expect( save ).toHaveBeenCalledWith( "things/views", expect.any(Array) );
			expect( persisted() ).toEqual( ["Mine"] );
		} );

		it( 'adds a System view saved under a new name as a User view', async ()=>{
			const { set } = create( [make( undefined ), make( "Users" )] );
			const view = View.copy( set.current() );
			view.name = "Mine";
			await set.save( view );
			expect( names(set) ).toEqual( ["Default:System", "Users:System", "Mine:User"] );
			expect( set.index() ).toBe( 2 );
			expect( localStorage.getItem("things/viewIndex") ).toBe( "2" );
		} );

		//reviews-issue/web-refactor-review.md #3:  the twin of a System view was promoted to a User view of the same name, beside the
		//System one, and persisted - two `Users` after every reload
		it( "leaves a System view's twin Adhoc, and persists nothing", async ()=>{
			const { set, save } = create( [make( undefined ), make( "Users" )], 1 );
			set.sort( {active: "kind", direction: "asc"} );
			await set.save( View.copy(set.current()) );
			expect( names(set) ).toEqual( ["Default:System", "Users:System", "Users:Adhoc"] );
			expect( set.current().sort[0].active ).toBe( "kind" );
			expect( save ).not.toHaveBeenCalled();
			expect( localStorage.getItem("things/viewIndex") ).toBeNull();
		} );

		it( 'leaves a System view under a System name a System view, and persists nothing', async ()=>{
			const { set, save } = create( [make( undefined ), make( "Users" )], 1 );
			await set.save( View.copy(set.current()) );
			expect( names(set) ).toEqual( ["Default:System", "Users:System"] );
			expect( save ).not.toHaveBeenCalled();
		} );

		it( 'persists under the viewsKey, without what the route already says', async ()=>{
			const save = vi.fn().mockResolvedValue( undefined );
			const set = new ViewSet( {type: View, key: "users", viewsKey: "qlList/users/views", profileStore: {save} as unknown as ProfileStore, prefs: new LocalPrefs(), settings: {columns: ["name", "kind"], sort: [{active: "name", direction: "asc"}]}} );
			set.reset( [make( undefined )] );
			const view = View.copy( set.current() );
			view.name = "Mine";
			await set.save( view );
			expect( save.mock.lastCall![0] ).toBe( "qlList/users/views" );
			expect( save.mock.lastCall![1][0].sort ).toBeUndefined();//the route's own sort is not repeated in the saved view
		} );
	} );

	describe( 'remove', ()=>{
		it( 'takes the saved view and its twin, never a System view of the name', async ()=>{
			const { set, persisted } = create( [make( undefined ), make( "Users" ), make( "Users", ViewType.User ), make( "Users", ViewType.Adhoc )], 2 );
			await set.remove( set.current() );
			expect( names(set) ).toEqual( ["Default:System", "Users:System"] );
			expect( set.index() ).toBe( 0 );
			expect( localStorage.getItem("things/viewIndex") ).toBeNull();//index 0 is the default, which is stored as no entry
			expect( persisted() ).toEqual( [] );
		} );

		it( 'keeps the selection where it survived', async ()=>{
			const { set, persisted } = create( [make( undefined ), make( "A", ViewType.User ), make( "B", ViewType.User )], 2 );
			await set.remove( set.views()[1] );
			expect( set.current().name ).toBe( "B" );
			expect( set.index() ).toBe( 1 );
			expect( localStorage.getItem("things/viewIndex") ).toBe( "1" );//a stale index would restore a different view after a reload
			expect( persisted() ).toEqual( ["B"] );
		} );

		//reviews-issue/web-refactor-review.md #9:  the last view taken read views[0] of [], a throw out of remove() that no host's .catch saw
		it( 'takes the last view without throwing', async ()=>{
			localStorage.setItem( "things/viewIndex", "1" );
			const { set, persisted } = create( [make( "A", ViewType.User )] );
			await set.remove( set.current() );
			expect( set.views() ).toEqual( [] );
			expect( persisted() ).toEqual( [] );
			expect( localStorage.getItem("things/viewIndex") ).toBe( "1" );
		} );

		it( 'ignores anything but a User view', async ()=>{
			const { set, save } = create( [make( undefined ), make( "Users" )], 1 );
			await set.remove( set.current() );
			expect( names(set) ).toEqual( ["Default:System", "Users:System"] );
			expect( save ).not.toHaveBeenCalled();
		} );
	} );

	it( 'names the named System views - what the settings panel will not save under', ()=>{
		const { set } = create( [make( undefined ), make( "Users" ), make( "Deleted" ), make( "Mine", ViewType.User )] );
		expect( set.systemNames() ).toEqual( ["Users", "Deleted"] );
	} );
} );