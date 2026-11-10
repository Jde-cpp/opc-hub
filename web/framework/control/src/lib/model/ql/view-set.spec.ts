import { Injector, runInInjectionContext } from '@angular/core';
import { IPROFILE_SERVICE, LocalPrefs, ProfileStore } from 'jde-spa';
import { Operator, View, ViewType } from './view';
import { ViewSet, ViewSetOptions } from './view-set';
import { id, required, scalar, schemaOf } from 'jde-framework/testing';
import { flush, MockProfileService, silence } from 'jde-spa/testing';

//reviews/angular-refactor.md A1:  QLList, LogDetail and NodeChildren each kept their own copy of these rules, and each had fixed
//a different subset of the same bugs.
//#587:  a view is known by its number, the profile holds a row a view and the index, and the route's views are a starting set.
//#589:  a control changes a view on its draft, one a view, kept in the browser under the user's key.  `1:A*` is view 1 under a draft.
const schema = schemaOf( "Thing", id(), required("name"), scalar("kind"), scalar("deleted", "DateTime") );
class ThingView extends View{}//a host's subclass - LogView, NodeView

describe( 'ViewSet', ()=>{
	const make = ( name:string|undefined, sort="name" )=>ThingView.fromConfig( {name, configColumns: ["name", "kind"], sort: [{active: sort, direction: "asc"}]}, schema );
	const route = ()=>[make( undefined ), make( "Users" )];
	//a view that lists deleted rows, and a pick list's test for the views it offers
	const trash = ()=>{
		const view = make( "Trash" );
		view.fieldFilters = [{field: schema.fields.find( f=>f.name=="deleted" )!, filter: {operator: Operator.Greater, value: ["<not null>"]}}];
		return view;
	};
	const offered = ( view:ThingView )=>!view.fieldFilters.some( ff=>ff.field.name=="deleted" );
	//the profile rows of a saved set:  the views by number, and the index in their order unless it is given
	const stored = ( views:[number, ThingView][], index:number[]=views.map( ([number])=>number ) )=>
		Object.fromEntries( [["things/views", index], ...views.map( ([number, view])=>[`things/views/${number}`, view.toJson(undefined)] )] );
	//A set over a profile holding `rows`.  writes:  the keys written since it was last asked, in order, a deletion as `-key`.
	//held:  the index and the numbers that have a row.
	const create = ( rows:Record<string,unknown>={}, options:Partial<ViewSetOptions<ThingView>>={} )=>{
		const service = new MockProfileService();
		for( const [key, value] of Object.entries(rows) )
			service.rows.set( key, JSON.stringify(value) );
		const profileStore = runInInjectionContext( Injector.create({providers: [{provide: IPROFILE_SERVICE, useValue: service}]}), ()=>new ProfileStore() );
		const set = new ViewSet( {type: ThingView, key: "things", profileStore, prefs: new LocalPrefs(), ...options} );
		const writes = ()=>{
			const keys = service.save.mock.calls.map( ([key, value])=>value==null ? `-${key}` : key );
			service.save.mockClear();
			return keys;
		};
		const held = ()=>({
			index: JSON.parse( service.rows.get("things/views") ?? "null" ) as number[]|null,
			rows: [...service.rows.keys()].filter( key=>key.startsWith("things/views/") ).map( key=>+key.slice(13) ).sort( (a,b)=>a-b )
		});
		return { set, service, writes, held };
	};
	//loaded:  the route's two views over a profile holding `rows`
	const load = async ( rows:Record<string,unknown>={}, options:Partial<ViewSetOptions<ThingView>>={} )=>{
		const made = create( rows, options );
		await made.set.load( route(), schema );
		return made;
	};
	const names = ( set:ViewSet<ThingView> )=>set.views().map( v=>`${v.number}:${v.name ?? "Default"}${v.isAdhoc ? "*" : ""}` );
	//a view to save as a new one:  a copy under no number
	const unnumbered = ( set:ViewSet<ThingView>, name:string )=>{
		const view = View.copy( set.current() );
		view.name = name;
		view.number = 0;
		return view;
	};
	const remembered = ()=>localStorage.getItem( "things/viewNumber" );
	beforeEach( ()=>localStorage.clear() );

	describe( 'load', ()=>{
		it( "shows the route's views, numbered in its order, while none is saved", async ()=>{
			const { set, service, writes } = await load();
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			expect( set.views().every( v=>v.isSystem ) ).toBe( true );
			expect( service.loadRows ).toHaveBeenCalledWith( "things/views" );
			expect( writes() ).toEqual( [] );
		} );

		it( "shows the saved views in the index's order, as its type, in place of the route's", async ()=>{
			const { set } = await load( stored([[2, make("B")], [5, make("A")]], [5, 2]) );
			expect( names(set) ).toEqual( ["5:A", "2:B"] );
			expect( set.current() ).toBeInstanceOf( ThingView );
			expect( set.views().every( v=>v.isUser ) ).toBe( true );
			expect( set.routeDefault!.name ).toBeUndefined();//the route's are kept
		} );

		it( 'opens on the remembered number, on the first where it is gone, and writes neither', async ()=>{
			localStorage.setItem( "things/viewNumber", "5" );
			expect( (await load( stored([[2, make("B")], [5, make("A")]]) )).set.current().name ).toBe( "A" );
			localStorage.setItem( "things/viewNumber", "9" );
			expect( (await load( stored([[2, make("B")], [5, make("A")]]) )).set.current().name ).toBe( "B" );
			expect( remembered() ).toBe( "9" );
		} );

		it( 'ignores a row the index does not name and an index entry with no row, and leaves both as they are', async ()=>{
			const { set, writes, held } = await load( {...stored([[1, make("A")], [3, make("C")], [4, make("Orphan")]], [1, 7, 3])} );
			expect( names(set) ).toEqual( ["1:A", "3:C"] );
			expect( writes() ).toEqual( [] );
			await set.move( set.views()[1], 0 );//the next change writes the index as the set has it
			expect( held() ).toEqual( {index: [3, 1], rows: [1, 3, 4]} );
		} );

		it( "shows the route's views where the index names no row that is there", async ()=>{
			const { set } = await load( {"things/views": [7], "things/views/4": make("Orphan").toJson(undefined)} );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
		} );
	} );

	describe( 'numbers', ()=>{
		it( 'finds, replaces and removes by number:  two views of one name', async ()=>{
			const { set, writes } = await load( stored([[1, make("Same")], [2, make("Same")]]) );
			const edited = View.copy( set.views()[1] );
			edited.sort = [{active: "kind", direction: "desc"}];
			const saving = set.save( edited );
			expect( names(set) ).toEqual( ["1:Same", "2:Same"] );//the views have changed when it returns
			expect( set.views().map( v=>v.sort[0].active ) ).toEqual( ["name", "kind"] );
			expect( set.current().number ).toBe( 2 );
			await saving;
			expect( writes() ).toEqual( ["things/views/2"] );
			await set.remove( set.views()[0] );
			expect( names(set) ).toEqual( ["2:Same"] );
			expect( set.current().sort[0].active ).toBe( "kind" );
		} );

		it( 'adds a view saved under no number as the next above the highest, after the last, and shows it', async ()=>{
			const { set, writes, held } = await load( stored([[5, make("A")], [2, make("B")]]) );
			await set.save( unnumbered(set, "New") );
			expect( names(set) ).toEqual( ["5:A", "2:B", "6:New"] );
			expect( set.current() ).toBeInstanceOf( ThingView );//ql-list-settings emits a base View
			expect( set.current().number ).toBe( 6 );
			expect( remembered() ).toBe( "6" );
			expect( writes() ).toEqual( ["things/views/6", "things/views"] );//the row, then the index that names it
			expect( held() ).toEqual( {index: [5, 2, 6], rows: [2, 5, 6]} );
		} );

		it( 'adds a view whose number is no longer in the set', async ()=>{
			const { set } = await load( stored([[1, make("A")], [2, make("B")]]) );
			const gone = View.copy( set.views()[1] );
			await set.remove( gone );
			await set.save( gone );
			expect( names(set) ).toEqual( ["1:A", "2:B"] );
		} );
	} );

	describe( 'the starting set', ()=>{
		it( "keeps the whole set as the user's on the first save, every row and then the index", async ()=>{
			const { set, writes, held } = await load();
			set.select( 1 );
			set.sort( {active: "kind", direction: "asc"} );
			await set.save( View.copy(set.current()) );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );//the twin went over its view
			expect( set.current().isUser ).toBe( true );
			expect( set.current().sort[0].active ).toBe( "kind" );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );
			set.sort( {active: "name", direction: "desc"} );
			await set.save( View.copy(set.current()) );
			expect( writes() ).toEqual( ["things/views/2"] );//saved now:  a changed view's row alone
		} );

		it( 'saves the set with a new view after it', async ()=>{
			const { set, writes } = await load();
			await set.save( unnumbered(set, "Mine") );
			expect( names(set) ).toEqual( ["1:Default", "2:Users", "3:Mine"] );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views/3", "things/views"] );
		} );

		it( 'saves the set without a view deleted from it', async ()=>{
			const { set, writes, held } = await load();
			const kept = set.views()[1];
			await set.remove( set.views()[0] );
			expect( names(set) ).toEqual( ["2:Users"] );
			expect( set.current() ).toBe( kept );//a view the change left alone is the object it was
			expect( writes() ).toEqual( ["things/views/2", "things/views"] );
			expect( held() ).toEqual( {index: [2], rows: [2]} );
		} );

		it( "reads a saved set back in place of the route's, whatever the route has become", async ()=>{
			const first = await load();
			await first.set.rename( first.set.views()[1], "People" );
			const again = create();
			again.service.rows = first.service.rows;
			await again.set.load( [make(undefined), make("Users"), make("New in this release")], schema );
			expect( names(again.set) ).toEqual( ["1:Default", "2:People"] );
		} );

		it( 'leaves out of a saved view what the route already says', async ()=>{
			const { set, service } = create( {}, {viewsKey: "qlList/things/views", settings: {columns: ["name", "kind"], sort: [{active: "name", direction: "asc"}]}} );
			await set.load( route(), schema );
			await set.save( unnumbered(set, "Mine") );
			expect( JSON.parse(service.rows.get("qlList/things/views")!) ).toEqual( [1, 2, 3] );
			expect( JSON.parse(service.rows.get("qlList/things/views/3")!).sort ).toBeUndefined();//the route's own sort is not repeated
		} );
	} );

	describe( 'the last view deleted', ()=>{
		it( "shows the route's views at once, and deletes the index and then the row", async ()=>{
			const { set, writes, held } = await load( stored([[1, make("A")]]) );
			const removing = set.remove( set.current() );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			expect( set.current().isSystem ).toBe( true );
			await removing;
			expect( writes() ).toEqual( ["-things/views", "-things/views/1"] );
			expect( held() ).toEqual( {index: null, rows: []} );
		} );

		//reviews-issue/web-refactor-review.md #9:  the last view taken read views[0] of [], a throw out of remove() no host's .catch saw
		it( 'leaves a starting set of one as it is, and writes nothing', async ()=>{
			const { set, writes } = create();
			await set.load( [make(undefined)], schema );
			const only = set.current();
			await set.remove( only );
			expect( set.current() ).toBe( only );
			expect( writes() ).toEqual( [] );
		} );
	} );

	describe( 'a change', ()=>{
		it( 'writes a reorder as the index alone, and leaves the selection on its view', async ()=>{
			const { set, writes, held } = await load( stored([[1, make("A")], [2, make("B")], [3, make("C")]]) );
			await set.move( set.views()[2], 0 );
			expect( names(set) ).toEqual( ["3:C", "1:A", "2:B"] );
			expect( set.current().name ).toBe( "A" );
			expect( writes() ).toEqual( ["things/views"] );
			expect( held().index ).toEqual( [3, 1, 2] );
			await set.move( set.views()[0], 0 );//where it is
			expect( writes() ).toEqual( [] );
		} );

		it( "writes a deletion's index before its row, and keeps the selection where it survived", async ()=>{
			const { set, writes } = await load( stored([[1, make("A")], [2, make("B")], [3, make("C")]]) );
			set.select( 2 );
			await set.remove( set.views()[1] );
			expect( names(set) ).toEqual( ["1:A", "3:C"] );
			expect( set.current().name ).toBe( "C" );
			expect( remembered() ).toBe( "3" );//by number:  its place moved
			expect( writes() ).toEqual( ["things/views", "-things/views/2"] );
			await set.remove( set.current() );
			expect( set.current().name ).toBe( "A" );
			expect( remembered() ).toBe( "1" );
		} );

		it( 'renames a view and its draft, and writes the row as it was saved', async ()=>{
			const { set, service, writes } = await load( stored([[1, make("A")]]) );
			set.sort( {active: "kind", direction: "asc"} );
			await set.rename( set.current(), "Z" );
			expect( names(set) ).toEqual( ["1:Z*"] );
			expect( set.current().isAdhoc ).toBe( true );
			expect( writes() ).toEqual( ["things/views/1"] );
			expect( JSON.parse(service.rows.get("things/views/1")!) ).toMatchObject( {name: "Z", sort: [{active: "name"}]} );//not the draft's sort
		} );

		it( "duplicates a view after it, under its name and the next number, and shows the copy as the user's", async ()=>{
			const { set, writes, held } = await load( stored([[1, make("A")], [3, make("B")]]) );
			set.sort( {active: "kind", direction: "asc"} );//a draft, which stays its view's
			await set.duplicate( set.views()[0] );
			expect( names(set) ).toEqual( ["1:A*", "4:A", "3:B"] );
			expect( set.current().number ).toBe( 4 );
			expect( set.current().isUser ).toBe( true );
			expect( set.current().sort[0].active ).toBe( "name" );//the view's, not its draft's
			expect( remembered() ).toBe( "4" );
			expect( writes() ).toEqual( ["things/views/4", "things/views"] );
			expect( held() ).toEqual( {index: [1, 4, 3], rows: [1, 3, 4]} );
		} );

		//#588 review #6:  the toolbar's Duplicate passes the view on screen, which on an edited tab is the draft
		it( 'duplicates the view under a draft, not the draft, which its view keeps', async ()=>{
			const { set } = await load( stored([[1, make("A")]]) );
			set.sort( {active: "kind", direction: "asc"} );
			await set.duplicate( set.current() );
			expect( names(set) ).toEqual( ["1:A*", "2:A"] );
			expect( set.current().sort[0].active ).toBe( "name" );
		} );

		//#588 review #4:  the host re-reads on it
		it( 'tells the host at once of a change that puts another view on screen, and of none that leaves it there', async ()=>{
			const { set } = await load( stored([[1, make("A")], [2, make("B")]]) );
			const shown = vi.fn();
			await set.move( set.current(), 1, shown );
			expect( shown ).not.toHaveBeenCalled();
			const renamed = set.rename( set.current(), "Z", shown );
			expect( shown ).toHaveBeenCalledTimes( 1 );//before its write
			await renamed;
			expect( shown ).toHaveBeenCalledTimes( 1 );
		} );

		it( "adds a copy of the view shown, its draft's changes with it, named for its number and after the last", async ()=>{
			const { set, writes, held } = await load();
			set.sort( {active: "kind", direction: "asc"} );
			await set.add();
			expect( names(set) ).toEqual( ["1:Default*", "2:Users", "3:View 3"] );//the draft stays its view's
			expect( set.current().sort[0].active ).toBe( "kind" );
			expect( set.current().isUser ).toBe( true );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views/3", "things/views"] );//a first save:  the whole set
			expect( held() ).toEqual( {index: [1, 2, 3], rows: [1, 2, 3]} );
		} );

		it( 'writes nothing for a view that is not in the set', async ()=>{
			const { set, writes } = await load( stored([[1, make("A")]]) );
			const stranger = unnumbered( set, "Stranger" );
			await Promise.all( [set.remove( stranger ), set.rename( stranger, "x" ), set.move( stranger, 0 )] );
			expect( names(set) ).toEqual( ["1:A"] );
			expect( writes() ).toEqual( [] );
		} );
	} );

	//ProfileStore orders the writes of one key alone:  a change's writes wait for the last change's, and say what the set is by then
	describe( 'two changes in one turn', ()=>{
		//the next write held on its way:  the function returned lets it land
		const holdNext = ( service:MockProfileService )=>{
			let release!:()=>void;
			service.save.mockImplementationOnce( ( key:string, value:string|null )=>new Promise<void>( done=>{ release = ()=>{ service.rows.set( key, value! ); done(); }; } ) );
			return ()=>release();
		};

		it( "holds the second's writes until the first's are done, and leaves the index and the rows agreeing", async ()=>{
			const { set, service, writes, held } = await load( stored([[1, make("A")], [2, make("B")]]) );
			const release = holdNext( service );
			const added = set.save( unnumbered(set, "New") ), moved = set.move( set.current(), 0 );
			expect( names(set) ).toEqual( ["3:New", "1:A", "2:B"] );
			await flush();
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );//the new row is on its way, and the index does not name it yet
			release();
			await Promise.all( [added, moved] );
			expect( writes() ).toEqual( ["things/views/3", "things/views"] );
			expect( held() ).toEqual( {index: [3, 1, 2], rows: [1, 2, 3]} );
		} );

		it( 'writes nothing for a new view deleted at once', async ()=>{
			const { set, writes, held } = await load( stored([[1, make("A")]]) );
			const added = set.save( unnumbered(set, "New") ), removed = set.remove( set.current() );
			await Promise.all( [added, removed] );
			expect( names(set) ).toEqual( ["1:A"] );
			expect( writes() ).toEqual( [] );
			expect( held() ).toEqual( {index: [1], rows: [1]} );
		} );

		it( 'makes a failed write again with the next change, the index after the row it names', async ()=>{
			const { set, service, writes, held } = await load( stored([[1, make("A")], [2, make("B")]]) );
			service.save.mockRejectedValueOnce( new Error('down') );
			await expect( set.save( unnumbered(set, "New") ) ).rejects.toThrow( 'down' );
			expect( names(set) ).toEqual( ["1:A", "2:B", "3:New"] );
			expect( writes() ).toEqual( ["things/views/3"] );
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );//the index never named the row that is not there
			await set.move( set.views()[0], 1 );
			expect( writes() ).toEqual( ["things/views/2", "things/views/1", "things/views/3", "things/views"] );//in the new order:  the store no longer knows what the server holds
			expect( held() ).toEqual( {index: [2, 1, 3], rows: [1, 2, 3]} );
		} );

		//review #2:  the index came from the set as it stood after the row writes, so it named a view whose row was still to come
		it( "names in a change's index no view added while its rows were written", async ()=>{
			const { set, service, writes, held } = await load( stored([[1, make("A")]]) );
			const release = holdNext( service );
			const renamed = set.rename( set.current(), "Z" );
			await flush();
			const added = set.save( unnumbered(set, "New") );
			release();
			await renamed;
			expect( held() ).toEqual( {index: [1], rows: [1]} );
			await added;
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );
		} );

		//review #13:  one round trip a row, on a page that shows nothing until they finish
		it( 'writes the rows together, and then the index', async ()=>{
			const { set, service, writes } = await load();
			const release = holdNext( service );
			const renamed = set.rename( set.views()[1], "Mine" );
			await flush();
			expect( service.save.mock.calls.map( ([key])=>key ) ).toEqual( ["things/views/1", "things/views/2"] );//the first is still on its way
			release();
			await renamed;
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
		} );
	} );

	//review #1 and #5:  signed out, a read answers no rows and a write goes nowhere, and neither may pass for the profile
	describe( 'signing in and out', ()=>{
		it( "reads for whoever signed in since, and shows their views in place of the route's", async ()=>{
			const { set, service, writes, held } = create( stored([[1, make("Mine")]]) );
			service.setUser( undefined );
			await set.load( route(), schema );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			service.setUser( 'u1' );
			await expect( set.remove( set.views()[1] ) ).rejects.toThrow( 'make the change again' );
			expect( names(set) ).toEqual( ["1:Mine"] );
			expect( writes() ).toEqual( [] );
			expect( held() ).toEqual( {index: [1], rows: [1]} );
		} );

		it( "makes the change for one who signed in with no views saved", async ()=>{
			const { set, service, writes } = create();
			service.setUser( undefined );
			await set.load( route(), schema );
			service.setUser( 'u1' );
			await set.rename( set.views()[1], "Mine" );
			expect( names(set) ).toEqual( ["1:Default", "2:Mine"] );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
		} );

		it( 'keeps a change made signed out here, and writes it with the next one signed in', async ()=>{
			const { set, service, writes, held } = await load( stored([[1, make("A")], [2, make("B")]]) );
			service.setUser( undefined );
			await set.save( unnumbered(set, "New") );
			await set.remove( set.views()[0] );
			expect( names(set) ).toEqual( ["2:B", "3:New"] );
			expect( writes() ).toEqual( [] );
			service.setUser( 'u1' );
			await set.move( set.views()[1], 0 );
			expect( names(set) ).toEqual( ["3:New", "2:B"] );
			expect( held() ).toEqual( {index: [3, 2], rows: [2, 3]} );
			expect( service.loadRows ).toHaveBeenCalledTimes( 1 );//the same user's:  no read
		} );

		it( "does not carry one user's views over to another who has none", async ()=>{
			const { set, service, writes } = await load( stored([[1, make("A")]]) );
			service.rows.clear();
			service.setUser( 'u2' );
			await expect( set.rename( set.current(), "x" ) ).rejects.toThrow( 'make the change again' );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			expect( writes() ).toEqual( [] );
		} );
	} );

	describe( 'canRemove', ()=>{
		it( 'takes a view of a saved set, the last of them too', async ()=>{
			const { set } = await load( stored([[1, make("A")]]) );
			expect( set.canRemove( set.current() ) ).toBe( true );
		} );

		it( "takes one of a route's several", async ()=>{
			const { set } = await load();
			expect( set.canRemove( set.views()[1] ) ).toBe( true );
		} );

		//review #7:  it would come straight back
		it( "does not take the route's only view", async ()=>{
			const { set } = create();
			await set.load( [make(undefined)], schema );
			expect( set.canRemove( set.current() ) ).toBe( false );
		} );

		//#589:  Discard drops the changes alone, so Delete takes the view with them
		it( 'takes a view under its draft', async ()=>{
			const { set } = await load( stored([[1, make("A")], [2, make("B")]]) );
			set.sort( {active: "kind", direction: "asc"} );
			expect( set.current().isAdhoc ).toBe( true );
			expect( set.canRemove( set.current() ) ).toBe( true );
			expect( set.canRemove( View.copy(set.current()) ) ).toBe( true );//the copy the settings panel edits
		} );

		it( 'does not take the fallback a pick list shows under no number', async ()=>{
			const { set } = await load( stored([[1, trash()]]), {offered} );
			expect( set.current().number ).toBe( 0 );
			expect( set.canRemove( set.current() ) ).toBe( false );
		} );
	} );

	//no rows and a failed read must not look alike:  the first save of a starting set writes every row
	describe( 'a failed read', ()=>{
		afterEach( ()=>vi.restoreAllMocks() );
		const failing = async ( rows:Record<string,unknown>={} )=>{
			const made = create( rows );
			made.service.loadRows.mockRejectedValueOnce( new Error('down') );
			const warn = silence();
			await made.set.load( route(), schema );
			expect( warn ).toHaveBeenCalled();
			return made;
		};

		it( "shows the route's views, and refuses every change with the read's error until a read succeeds", async ()=>{
			const { set, service, writes } = await failing();
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			for( const change of [()=>set.save( View.copy(set.current()) ), ()=>set.rename( set.current(), "x" ), ()=>set.move( set.current(), 1 ), ()=>set.remove( set.current() )] ){
				service.loadRows.mockRejectedValueOnce( new Error('still down') );
				await expect( change() ).rejects.toThrow( 'still down' );
			}
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			expect( writes() ).toEqual( [] );
		} );

		it( 'tries the read again with the next change, and makes the change once it succeeds', async ()=>{
			const { set, writes } = await failing();
			const renamed = set.rename( set.views()[1], "Mine" ), moved = set.move( set.views()[1], 0 );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );//not before the read
			await Promise.all( [renamed, moved] );
			expect( names(set) ).toEqual( ["2:Mine", "1:Default"] );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views", "things/views"] );//each made in its turn:  the first save, then the new order
			const again = set.rename( set.views()[0], "Ours" );
			expect( names(set) ).toEqual( ["2:Ours", "1:Default"] );//read now:  at once
			await again;
		} );

		it( "shows the saved views the retry finds, and refuses the change made to the route's", async ()=>{
			const { set, writes, held } = await failing( stored([[2, make("Mine")]]) );
			await expect( set.remove( set.views()[1] ) ).rejects.toThrow( 'make the change again' );//the route's Users, which the saved view 2 is not
			expect( names(set) ).toEqual( ["2:Mine"] );
			expect( writes() ).toEqual( [] );
			expect( held() ).toEqual( {index: [2], rows: [2]} );
			await set.rename( set.current(), "Ours" );
			expect( writes() ).toEqual( ["things/views/2"] );
		} );

		//#588 review #4:  the host told once the change is made, not of what the user did while the read was out
		it( 'tells the host of the view a change puts on screen once the read lands, and of no tab picked meanwhile', async ()=>{
			const { set } = await failing();
			const shown = vi.fn();
			const moved = set.move( set.views()[1], 0, shown );
			set.select( 1 );//the user's, while the read is out
			await moved;
			expect( shown ).not.toHaveBeenCalled();
			expect( names(set) ).toEqual( ["2:Users", "1:Default"] );
			const unread = await failing();
			const added = unread.set.add( shown );
			expect( shown ).not.toHaveBeenCalled();
			await added;
			expect( shown ).toHaveBeenCalledTimes( 1 );
			expect( unread.set.current().name ).toBe( "View 3" );
		} );

		it( "tells the host of the saved views the read puts on screen in the route's place", async ()=>{
			const { set } = await failing( stored([[2, make("Mine")]]) );
			const shown = vi.fn();
			await expect( set.remove( set.views()[1], shown ) ).rejects.toThrow( 'make the change again' );
			expect( shown ).toHaveBeenCalledTimes( 1 );
		} );

		//reviews-issue/589-views-draft.md #12:  the toolbar's Save took the draft at the click, and then dropped the one changed while
		//the read was out
		it( "saves a view's draft as it stands once the read lands", async ()=>{
			const { set, service } = await failing();
			set.sort( {active: "kind", direction: "asc"} );
			const saving = set.saveDraft( set.current() );
			set.edit( view=>{ view.limit = 100; } );
			await saving;
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			const row = JSON.parse( service.rows.get("things/views/1")! );
			expect( [row.sort[0].active, row.limit] ).toEqual( ["kind", 100] );
		} );
	} );

	//a pick list offers no view of the deleted rows, on a set that is the list page's too
	describe( 'a view the host does not offer', ()=>{
		it( 'stays out of the views, and in what a first save writes', async ()=>{
			const { set, writes, held } = create( {}, {offered} );
			await set.load( [make(undefined), trash()], schema );
			expect( names(set) ).toEqual( ["1:Default"] );
			set.sort( {active: "kind", direction: "asc"} );
			await set.save( View.copy(set.current()) );
			expect( names(set) ).toEqual( ["1:Default"] );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );
		} );

		it( 'opens on the first that is offered where the remembered view is not, without writing the selection', async ()=>{
			localStorage.setItem( "things/viewNumber", "1" );
			const { set } = await load( stored([[1, trash()], [2, make("A")], [3, make("B")]]), {offered} );
			expect( names(set) ).toEqual( ["2:A", "3:B"] );
			expect( set.current().name ).toBe( "A" );
			set.sort( {active: "kind", direction: "asc"} );
			expect( remembered() ).toBe( "1" );
			set.select( 1 );
			expect( remembered() ).toBe( "3" );//a pick is the user's word
		} );

		it( "shows the route's default view under no number where none is offered, and a Save adds it to the set", async ()=>{
			const { set, writes, held } = await load( stored([[1, trash()]]), {offered} );
			expect( names(set) ).toEqual( ["0:Default"] );
			set.select( 0 );
			expect( remembered() ).toBeNull();
			await set.remove( set.current() );//not the set's
			expect( writes() ).toEqual( [] );
			await set.save( View.copy(set.current()) );
			expect( names(set) ).toEqual( ["2:Default"] );
			expect( writes() ).toEqual( ["things/views/2", "things/views"] );
			expect( held() ).toEqual( {index: [1, 2], rows: [1, 2]} );
		} );

		//reviews-issue/589-views-draft.md #9:  the list page's draft of a view, filtered on the deleted rows, put them in a pick list
		it( "shows a view as saved where its draft is one it would not offer", async ()=>{
			const page = await load( stored([[1, make("A")], [2, make("B")]]) );
			page.set.edit( view=>{ view.fieldFilters = trash().fieldFilters; } );
			const { set } = await load( stored([[1, make("A")], [2, make("B")]]), {offered} );
			expect( names(set) ).toEqual( ["1:A", "2:B"] );
			expect( set.current().fieldFilters ).toEqual( [] );
		} );
	} );

	//a draft needs no read:  the route's views alone
	const unread = ()=>{
		const { set } = create();
		set.reset( route(), schema );
		return set;
	};
	const kindFirst = ( view:View )=>{
		const copy = View.copy( view );
		copy.sort = [{active: "kind", direction: "desc"}];
		return copy;
	};

	describe( 'show', ()=>{
		it( "puts an Adhoc draft in its view's place, under its number and name, and replaces it on the next show", ()=>{
			const set = unread();
			set.select( 1 );
			const shown = kindFirst( set.current() );
			shown.name = "the editor's";
			set.show( shown );
			expect( names(set) ).toEqual( ["1:Default", "2:Users*"] );
			expect( set.current() ).toBeInstanceOf( ThingView );//ql-list-settings emits a base View
			expect( shown.type ).toBe( ViewType.System );//the caller's copy is left alone
			const again = View.copy( set.current() );
			again.limit = 50;
			set.show( again );
			expect( names(set) ).toEqual( ["1:Default", "2:Users*"] );
			expect( [set.current().limit, set.current().sort[0].active] ).toEqual( [50, "kind"] );
		} );

		it( 'makes no draft of a view shown as it is, and drops one changed back', ()=>{
			const set = unread();
			const view = set.current();
			set.show( view );
			expect( set.current() ).toBe( view );
			set.show( kindFirst(view) );
			expect( set.current().isAdhoc ).toBe( true );
			set.show( View.copy(view) );
			expect( set.current() ).toBe( view );
		} );

		it( 'leaves the remembered number as it was', ()=>{
			const set = unread();
			set.show( kindFirst(set.current()) );
			expect( set.index() ).toBe( 0 );
			expect( remembered() ).toBeNull();//a Show is not a pick
		} );
	} );

	it( 'sorts on a draft, the column first and the rest kept', ()=>{
		const set = unread();
		set.current().sort = [{active: "name", direction: "asc"}, {active: "kind", direction: "asc"}];
		set.sort( {active: "kind", direction: "desc"} );
		expect( set.current().isAdhoc ).toBe( true );
		expect( set.current().sort ).toEqual( [{active: "kind", direction: "desc"}, {active: "name", direction: "asc"}] );
		expect( set.routeDefault!.sort[0].active ).toBe( "name" );//the view it came from is untouched
	} );

	it( 'edits the view shown on its draft', ()=>{
		const set = unread();
		set.edit( view=>{ view.limit = 100; } );
		expect( [set.current().limit, set.current().isAdhoc] ).toEqual( [100, true] );
		expect( set.routeDefault!.limit ).toBe( 25 );
	} );

	describe( 'select', ()=>{
		it( 'remembers the view by its number, and keeps the draft of the tab left', ()=>{
			const set = unread();
			set.sort( {active: "kind", direction: "asc"} );
			expect( set.select( 1 ) ).toBe( true );
			expect( names(set) ).toEqual( ["1:Default*", "2:Users"] );
			expect( set.current().name ).toBe( "Users" );
			expect( remembered() ).toBe( "2" );
			set.select( 0 );
			expect( [set.current().isAdhoc, set.current().sort[0].active] ).toEqual( [true, "kind"] );
		} );

		it( 'does nothing for an index that is not there', ()=>{
			const set = unread();
			expect( set.select( 3 ) ).toBe( false );
			expect( set.current().name ).toBeUndefined();
		} );
	} );

	describe( 'drafts', ()=>{
		const kind = {active: "kind", direction: "asc"} as const;
		const drafts = ( user="u1" )=>JSON.parse( localStorage.getItem(`${user}/things/drafts`) ?? "null" ) as Record<string,{name?:string, sort:{active:string}[], limit:number}>|null;
		const two = ()=>stored( [[1, make("A")], [2, make("B")]] );
		afterEach( ()=>vi.restoreAllMocks() );

		it( 'keeps one a view, each in its own tab', async ()=>{
			const { set, writes } = await load( two() );
			set.sort( kind );
			set.select( 1 );
			set.edit( view=>{ view.limit = 50; } );
			expect( names(set) ).toEqual( ["1:A*", "2:B*"] );
			expect( [set.views()[0].sort[0].active, set.views()[0].limit, set.views()[1].sort[0].active, set.views()[1].limit] ).toEqual( ["kind", 25, "name", 50] );
			expect( writes() ).toEqual( [] );//the browser's, not the profile's
		} );

		it( "writes each change to the browser under the user's key and the set's, by number and without the name", async ()=>{
			const { set } = await load( two() );
			set.sort( kind );
			set.select( 1 );
			set.edit( view=>{ view.limit = 50; } );
			expect( Object.keys(drafts()!) ).toEqual( ["1", "2"] );
			expect( drafts()![1].sort[0].active ).toBe( "kind" );
			expect( drafts()![2].limit ).toBe( 50 );
			expect( "name" in drafts()![1] ).toBe( false );
		} );

		it( "saves one over its view, the others kept", async ()=>{
			const { set, service, writes } = await load( two() );
			set.sort( kind );
			set.select( 1 );
			set.edit( view=>{ view.limit = 50; } );
			await set.save( set.current() );
			expect( names(set) ).toEqual( ["1:A*", "2:B"] );
			expect( [set.current().isUser, set.current().limit] ).toEqual( [true, 50] );
			expect( writes() ).toEqual( ["things/views/2"] );
			expect( JSON.parse(service.rows.get("things/views/2")!).limit ).toBe( 50 );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		it( "saves the whole set as the user's on a built-in view's Save, with that view changed", async ()=>{
			const { set, service, writes } = await load();
			set.select( 1 );
			set.sort( kind );
			await set.save( set.current() );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			expect( writes() ).toEqual( ["things/views/1", "things/views/2", "things/views"] );
			expect( JSON.parse(service.rows.get("things/views/2")!).sort[0].active ).toBe( "kind" );
			expect( drafts() ).toBeNull();
		} );

		it( 'discards one, the view as saved shown again, and says where there was none', async ()=>{
			const { set, writes } = await load( two() );
			const saved = set.current();
			set.sort( kind );
			expect( set.discard( set.current() ) ).toBe( true );
			expect( set.current() ).toBe( saved );
			expect( drafts() ).toBeNull();
			expect( set.discard( set.current() ) ).toBe( false );
			expect( writes() ).toEqual( [] );
		} );

		it( "rebuilds every tab's draft on a reload, on the remembered view", async ()=>{
			const first = await load( two() );
			first.set.sort( kind );
			first.set.select( 1 );
			first.set.edit( view=>{ view.limit = 50; } );
			const { set } = await load( two() );
			expect( names(set) ).toEqual( ["1:A*", "2:B*"] );
			expect( set.current() ).toBeInstanceOf( ThingView );
			expect( [set.current().number, set.current().limit, set.views()[0].sort[0].active] ).toEqual( [2, 50, "kind"] );
			await set.save( set.views()[0] );//a draft read back saves as one made here
			expect( names(set) ).toEqual( ["1:A", "2:B*"] );
		} );

		it( 'drops the draft of a view deleted, at once, so a new view taking its number has none', async ()=>{
			const { set } = await load( two() );
			set.select( 1 );
			set.sort( kind );
			await set.remove( set.current() );
			expect( drafts() ).toBeNull();
			await set.add();
			expect( names(set) ).toEqual( ["1:A", "2:View 2"] );
		} );

		it( 'drops one whose view is gone when it is read back, there too', async ()=>{
			localStorage.setItem( "u1/things/drafts", JSON.stringify({1: kindFirst( make("A") ).toJson(undefined), 7: make("Gone").toJson(undefined)}) );
			const { set } = await load( two() );
			expect( names(set) ).toEqual( ["1:A*", "2:B"] );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		it( 'drops one that can no longer be made a view of', async ()=>{
			localStorage.setItem( "u1/things/drafts", JSON.stringify({1: {fields: 5}}) );
			const warn = silence();
			const { set } = await load( two() );
			expect( warn ).toHaveBeenCalled();
			expect( names(set) ).toEqual( ["1:A", "2:B"] );
			expect( drafts() ).toBeNull();
		} );

		it( 'renames the view under a draft, so that neither a reload nor a Save puts the old name back', async ()=>{
			const first = await load( two() );
			first.set.sort( kind );
			await first.set.rename( first.set.current(), "Z" );
			const rows = Object.fromEntries( [...first.service.rows].map( ([key, value])=>[key, JSON.parse(value)] ) );
			const { set, service } = await load( rows );
			expect( names(set) ).toEqual( ["1:Z*", "2:B"] );
			await set.save( set.current() );
			const row = JSON.parse( service.rows.get("things/views/1")! );
			expect( [row.name, row.sort[0].active] ).toEqual( ["Z", "kind"] );
		} );

		it( "keeps a second user's apart", async ()=>{
			const first = await load( two() );
			first.set.sort( kind );
			const second = create( two() );
			second.service.setUser( 'u2' );
			await second.set.load( route(), schema );
			expect( names(second.set) ).toEqual( ["1:A", "2:B"] );
			second.set.select( 1 );
			second.set.edit( view=>{ view.limit = 50; } );
			expect( Object.keys(drafts("u2")!) ).toEqual( ["2"] );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		it( 'does not carry them over to one who signs in on the page', async ()=>{
			const { set, service } = await load();
			set.sort( kind );
			service.setUser( 'u2' );
			await set.rename( set.views()[1], "Mine" );//reads for u2, who has saved none:  the route's views stay
			expect( names(set) ).toEqual( ["1:Default", "2:Mine"] );
			expect( drafts("u2") ).toBeNull();
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		it( 'keeps one made signed out, or before the profile is read, here alone', async ()=>{
			const { set, service } = await load();
			service.setUser( undefined );
			set.sort( kind );
			expect( set.current().isAdhoc ).toBe( true );
			const unreadSet = unread();
			unreadSet.sort( kind );
			expect( unreadSet.current().isAdhoc ).toBe( true );
			expect( localStorage.length ).toBe( 0 );
		} );

		it( 'writes one made before the read once it lands, where the set is the one it was made on', async ()=>{
			const { set } = create();
			set.reset( route(), schema );
			set.sort( kind );
			await set.move( set.views()[1], 0 );
			expect( names(set) ).toEqual( ["2:Users", "1:Default*"] );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		it( "leaves a pick list's fallback's out of the browser", async ()=>{
			const { set } = await load( stored([[1, trash()]]), {offered} );
			set.sort( kind );
			expect( names(set) ).toEqual( ["0:Default*"] );
			expect( drafts() ).toBeNull();
			set.discard( set.current() );
			expect( names(set) ).toEqual( ["0:Default"] );
		} );

		//reviews-issue/589-views-draft.md #1:  toJson leaves out the route's name for a column, and the draft came back under the
		//schema's
		it( "reads one back under the route's names for its columns", async ()=>{
			const columns = ["name", {name: "kind", displayName: "Type"}];
			const route = ()=>[undefined, "Users"].map( name=>ThingView.fromConfig( {name, configColumns: columns, sort: [{active: "name", direction: "asc"}]}, schema ) );
			const settings = {settings: {columns, sort: [{active: "name", direction: "asc"} as const]}};
			const first = create( {}, settings );
			await first.set.load( route(), schema );
			first.set.sort( kind );
			const { set } = create( {}, settings );
			await set.load( route(), schema );
			expect( names(set) ).toEqual( ["1:Default*", "2:Users"] );
			expect( set.current().fields.find( f=>f.name=="kind" )!.displayName ).toBe( "Type" );
		} );

		//reviews-issue/589-views-draft.md #2:  a failed write had already taken the draft out of the browser, so a reload lost it
		it( 'keeps one in the browser until the profile holds its Save or its Delete', async ()=>{
			const { set, service } = await load( two() );
			set.sort( kind );
			set.select( 1 );
			set.sort( kind );
			service.save.mockRejectedValueOnce( new Error('offline') );
			await expect( set.saveDraft( set.current() ) ).rejects.toThrow( 'offline' );
			service.save.mockRejectedValueOnce( new Error('offline') );
			await expect( set.remove( set.views()[0] ) ).rejects.toThrow( 'offline' );
			expect( names(set) ).toEqual( ["2:B"] );
			expect( Object.keys(drafts()!) ).toEqual( ["1", "2"] );
			const reloaded = await load( two() );//the profile as it was:  neither write landed
			expect( names(reloaded.set) ).toEqual( ["1:A*", "2:B*"] );
		} );

		//reviews-issue/589-views-draft.md #4:  each tab wrote the whole entry from its own drafts, over the other's
		it( 'leaves those another tab wrote as they are', async ()=>{
			const one = await load( two() ), other = await load( two() );
			one.set.sort( kind );
			other.set.select( 1 );
			other.set.sort( kind );
			expect( Object.keys(drafts()!) ).toEqual( ["1", "2"] );
			other.set.discard( other.set.current() );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		//reviews-issue/589-views-draft.md #7:  one equal to its view, written by a tab that had not seen the view saved, showed the dot
		it( 'drops one that shows what its view does when it is read back', async ()=>{
			localStorage.setItem( "u1/things/drafts", JSON.stringify({1: make("A").toJson(undefined), 2: kindFirst( make("B") ).toJson(undefined)}) );
			const { set } = await load( two() );
			expect( names(set) ).toEqual( ["1:A", "2:B*"] );
			expect( Object.keys(drafts()!) ).toEqual( ["2"] );
		} );

		//reviews-issue/589-views-draft.md #8:  the editor's Save under a new name left the draft on the view it was opened on
		it( "drops the draft of the view shown on the editor's Save under another name", async ()=>{
			const { set } = await load( two() );
			set.sort( kind );
			await set.save( unnumbered(set, "Mine") );
			expect( names(set) ).toEqual( ["1:A", "2:B", "3:Mine"] );
			expect( set.current().sort[0].active ).toBe( "kind" );
			expect( drafts() ).toBeNull();
		} );

		//reviews-issue/589-views-draft.md #11:  the last user's stayed on screen until a change read for the new one, and that read
		//dropped the new one's too
		it( "takes the last user's off the screen as another signs in on the page, and keeps the new one's", async ()=>{
			const { set, service } = await load();
			set.sort( kind );
			service.setUser( 'u2' );
			expect( names(set) ).toEqual( ["1:Default", "2:Users"] );
			set.select( 1 );
			set.sort( kind );
			expect( names(set) ).toEqual( ["1:Default", "2:Users*"] );
			await set.rename( set.views()[0], "Mine" );
			expect( names(set) ).toEqual( ["1:Mine", "2:Users*"] );
			expect( Object.keys(drafts("u2")!) ).toEqual( ["2"] );
			expect( Object.keys(drafts()!) ).toEqual( ["1"] );
		} );

		//reviews-issue/589-views-draft.md #15:  every load wrote the entry, changed or not
		it( 'writes nothing to the browser at a load that changes no draft', async ()=>{
			const first = await load( two() );
			first.set.sort( kind );
			const setItem = vi.spyOn( Storage.prototype, 'setItem' );
			await load( two() );
			expect( setItem.mock.calls.filter( ([key])=>key.endsWith("/drafts") ) ).toEqual( [] );
		} );
	} );
} );