import { Flex, Style, View, ViewField, type ViewFieldSettings, type ViewSerializedArgs } from './view';
import { object, scalar, schemaOf, serverConnectionSchema } from 'jde-framework/testing';
import type { TableSettings } from '../../services/ql-list-resolver';

const schema = serverConnectionSchema();

describe( "View.query", ()=>{
	it( "emits an explicit sub-selection for an OBJECT column", ()=>{
		const view = View.fromTable( {columns: ["name", "url", {name:"opcSessions", displayName:"Sessions", selection:"count"}], sort: "name"}, schema );
		const query = view.query( 0 );
		expect( query.text ).toContain( "opcSessions{count}" );
		expect( query.text ).toContain( "serverConnections(" );
		expect( query.text ).not.toContain( "opcSessions " );//never the bare name - the server rejects a selection-less object field.
	} );
	it( "defaults an OBJECT column to the {id name} convention", ()=>{
		const view = View.fromTable( {columns: ["name", "provider"], sort: "name"}, schema );
		expect( view.query( 0 ).text ).toContain( "provider{id name}" );
	} );
	it( "round-trips selection through toJson", ()=>{
		const view = View.fromTable( {columns: [{name:"opcSessions", selection:"count"}], sort: "name"}, schema );
		const field = view.fields.find( f=>f.name=="opcSessions" )!;
		expect( field.toJson().selection ).toBe( "count" );
	} );
} );

//Deleted rows used to sit behind a Show deleted checkbox.  They are a filter now, like the Resources page's Enforced:  hidden
//unless the view filters on `deleted`, and never hidden on a live-toggle page, where `deleted` is the switch.
describe( "View.query on deleted rows", ()=>{
	const query = ( deleted:string|object="deleted" )=>View.fromTable( {columns: ["name", deleted], sort: "name"}, schema ).query( 0 );
	it( "leaves them out of a view that does not filter on deleted", ()=>{
		const q = query();
		expect( q.text ).toContain( "deleted:$deleted" );
		expect( q.vars["deleted"] ).toBeNull();
	} );
	it( "lists them on a live-toggle page", ()=>{
		expect( query({name: "deleted", liveToggle: {enable: "Enforce", disable: "Stop enforcing"}}).text ).not.toContain( "deleted:" );
	} );
	it( "adds no filter to a collection without the column", ()=>{
		const bare = schemaOf( "Thing", scalar("name") );
		expect( View.fromTable( {columns: ["name"], sort: "name"}, bare ).query( 0 ).text ).not.toContain( "deleted" );
	} );
	//reviews-issue/317-deleted-rows.md #1:  toJson drops a hidden Enforced column, so the saved view came back with a bare
	//`deleted` and query() filtered every unenforced resource out.
	describe( "on a saved view", ()=>{
		const enforced:ViewFieldSettings = {name: "deleted", displayName: "Enforced", liveToggle: {enable: "Enforce", disable: "Stop enforcing"}};
		const reload = ( columns:(string|ViewFieldSettings)[], hidden?:string )=>{
			const route = View.fromConfig( {configColumns: columns, sort: [{active: "name", direction: "asc"}]}, schema );
			const mine = View.copy( route );
			mine.name = "Mine";
			mine.fields = mine.fields.map( f=>{ const copy = new ViewField( f ); if( f.name==hidden ) copy.displayed = false; return copy; } );
			const saved:ViewSerializedArgs = JSON.parse( JSON.stringify(mine.toJson({columns, sort: "name"})) );
			return View.fromJson( saved, schema, route ).query( 0 );
		};
		it( "lists them on a live-toggle page with the column hidden", ()=>{
			expect( reload(["name", enforced], "deleted").text ).not.toContain( "deleted:" );
		} );
		it( "lists them on a live-toggle page with the column shown", ()=>{
			expect( reload(["name", enforced]).text ).not.toContain( "deleted:" );
		} );
		it( "still leaves them out elsewhere", ()=>{
			expect( reload(["name", "deleted"], "deleted").text ).toContain( "deleted:$deleted" );
		} );
	} );
} );

//#591:  the view's filter is the filter box's text, which query() compiles (filter-query.ts).  /access/resources is a live-toggle
//page - `deleted` is its Enforced switch - and a filter on it must still go out (reviews/m3-closing.md #16).
describe( "View.query on the filter", ()=>{
	const liveToggle = { enable: "Enforce", disable: "Stop enforcing" };
	const view = ( settings:{liveToggle?:typeof liveToggle}, filter:string, fixed="" )=>{
		const v = View.fromTable( {columns: ["name", {name: "deleted", displayName: "Enforced", ...settings}], sort: "name"}, schema );
		v.filter = filter;
		v.fixedFilter = fixed;
		return v.query( 0 );
	};
	it( 'sends has: on a live toggle - the enforced rows - as is null', ()=>{
		const q = view( {liveToggle}, "has:enforced" );
		expect( q.text ).toContain( "deleted:$deleted" );
		expect( q.vars["deleted"] ).toEqual( [null] );
	} );
	it( 'sends no: on a live toggle as not null', ()=>{
		expect( view({liveToggle}, "no:enforced").text ).toContain( "deleted:{ne:null}" );
	} );
	it( 'keeps a filter on deleted where it is only the trash can', ()=>{//the Deleted view is one
		const q = view( {}, "has:deleted" );
		expect( q.text ).toContain( "deleted:{ne:null}" );
		expect( q.text ).not.toContain( "deleted:$deleted" );
	} );
	it( 'sends a term, and leaves out one it cannot read', ()=>{
		const q = view( {}, "url:a,b nope:1 -description:x" );
		expect( q.text ).toContain( "url:$url" );
		expect( q.vars["url"] ).toEqual( ["a", "b"] );
		expect( q.text ).toContain( "description:{nin:$description_nin}" );
		expect( q.text ).not.toContain( "nope" );
		expect( q.text ).toContain( "deleted:$deleted" );//the deleted rows still left out
	} );
	it( "ands the host's fixed terms with the view's, as a repeated field is", ()=>{
		const q = view( {}, "has:deleted", "-id:3,4 no:deleted" );
		expect( q.text ).toContain( "id:{nin:$id_nin}" );
		expect( q.vars["id_nin"] ).toEqual( [3, 4] );
		expect( q.text ).toContain( "deleted:{in:$deleted_in,ne:null}" );//null and not null:  nothing
		expect( q.vars["deleted_in"] ).toEqual( [null] );
	} );
	it( 'takes the same filters in another order, or typed another way, as the same', ()=>{
		const of = ( filter:string )=>{ const v = View.fromTable( {columns: ["name", "url"], sort: "name"}, schema ); v.filter = filter; return v; };
		expect( View.sameFilters( of("url:a,b no:description"), of("no:description  URL:a,b") ) ).toBe( true );
		expect( View.sameFilters( of("url:a"), of("url:b") ) ).toBe( false );
		expect( View.sameFilters( of(""), of("nope:1") ) ).toBe( true );//a term that is not read filters nothing
	} );
} );

//A saved user view is JSON, so anything Style holds has to survive toJSON or the column reloads with the default
//alignment while the view it was edited from keeps its own.
describe( "Style serialization", ()=>{
	it( "keeps both the width and the alignment", ()=>{
		const style = new Style( {flex: new Flex(130), align: "right"} );
		expect( JSON.parse(JSON.stringify(style)) ).toEqual( {flex: "0 0 130px", align: "right"} );
	} );
	it( "omits what was not set", ()=>{
		expect( JSON.parse(JSON.stringify(new Style(90))) ).toEqual( {flex: "0 0 90px"} );
		expect( JSON.parse(JSON.stringify(new Style({align: "right"}))) ).toEqual( {align: "right"} );
	} );
	it( "carries the alignment onto a view field", ()=>{
		const view = View.fromTable( {columns: [{name:"opcSessions", selection:"count", style: new Style({align:"right"})}], sort: "name"}, schema );
		expect( view.fields.find(f=>f.name=="opcSessions")!.toJson().style ).toMatchObject( {align: "right"} );
	} );
} );

//A persisted view outlives the schema (groupings->groups already happened once):  the revived view has to prune, not throw -
//the throw landed inside loadViews and rejected QLListResolver, killing the page even on the default view.
describe( "View revival against a changed schema", ()=>{
	const serialized = ()=>(<any>{
		name: "Mine",
		collectionName: "serverConnections",
		fields: [ {name: "name"}, {name: "groupings"} ],
		filter: "groupings:a",
		showSelector: false,
		sort: [ {active: "name", direction: "asc"} ]
	});
	it( "drops a displayed column the schema no longer has", ()=>{
		const view = View.fromJson( serialized(), schema );
		const names = view.fields.map( f=>f.name );
		expect( names ).toContain( "name" );
		expect( names ).not.toContain( "groupings" );
	} );
	it( "keeps the text of a filter on a column the schema no longer has, as a term the box cannot read", ()=>{
		const view = View.fromJson( serialized(), schema );
		expect( view.filter ).toBe( "groupings:a" );
		expect( view.parsedFilter() ).toMatchObject( {terms: [], errors: [{start: 0, end: 11}]} );
	} );
	it( "still builds a query afterwards", ()=>{
		const view = View.fromJson( serialized(), schema );
		const query = view.query( 0 );
		expect( query.text ).toContain( "serverConnections(" );
		expect( query.text ).not.toContain( "groupings" );
	} );
	it( "keeps a filter whose column survives", ()=>{
		const args = serialized();
		args.filter = "url:a";
		expect( View.fromJson( args, schema ).query( 0 ).vars["url"] ).toEqual( ["a"] );
	} );
} );

//reviews/angular-refactor.md B13:  one static per source in place of a constructor that told four apart by shape.  A static makes
//the class it is called on, which is how a ViewSet of LogViews or NodeViews keeps their query() and apply().
describe( "View statics", ()=>{
	class ThingView extends View{}
	const base = View.fromTable( {columns: ["name", "url"], sort: "name"}, schema );

	it( "make the class they are called on", ()=>{
		const copy = ThingView.copy( base );
		expect( copy ).toBeInstanceOf( ThingView );
		expect( copy.fields.map( f=>f.name ) ).toEqual( base.fields.map( f=>f.name ) );
		const json = JSON.parse( JSON.stringify(base.toJson(undefined)) );
		expect( ThingView.fromJson( json, schema, base ) ).toBeInstanceOf( ThingView );
		expect( View.copy( copy ) ).not.toBeInstanceOf( ThingView );
	} );

	it( "copies the filter, and the host's fixed terms, and saves the filter alone", ()=>{
		const route = View.fromConfig( {configColumns: ["name"], sort: [], filter: "name:a"}, schema );
		route.fixedFilter = "-id:1";
		const copy = View.copy( route );
		expect( [copy.filter, copy.fixedFilter] ).toEqual( ["name:a", "-id:1"] );
		const json = JSON.parse( JSON.stringify(copy.toJson(undefined)) );
		expect( json.filter ).toBe( "name:a" );
		expect( JSON.stringify(json) ).not.toContain( "-id:1" );
		expect( View.fromJson( json, schema, route ).filter ).toBe( "name:a" );
		expect( "filter" in JSON.parse( JSON.stringify(View.fromConfig( {configColumns: ["name"], sort: []}, schema ).toJson(undefined)) ) ).toBe( false );
	} );
	it( "refuse a route's filter that does not read", ()=>{
		expect( ()=>View.fromConfig( {name: "Bad", configColumns: ["name"], sort: [], filter: "nope:1"}, schema ) ).toThrow( /nope/ );
	} );

	//#589:  a draft hides, re-styles and resizes its columns, and the view it is of stays as it was saved
	it( "copy each field, its style and its flex, the route's and a saved view's alike", ()=>{
		const route = View.fromTable( {columns: [{name: "name", style: new Style({flex: new Flex(300), align: "right"})}, "url"], sort: "name"}, schema );
		const saved = View.fromJson( JSON.parse(JSON.stringify(route.toJson(undefined))), schema, route );//its style a plain object, its flex the string toJSON wrote
		for( const view of [route, saved] ){
			const copy = View.copy( view );
			const [field, copied] = [view, copy].map( v=>v.fields.find( f=>f.name=="name" )! );
			expect( copied ).not.toBe( field );
			expect( copied.style ).not.toBe( field.style );
			expect( copied.style!.flex ).not.toBe( field.style!.flex );
			expect( View.alike(copy, view) ).toBe( true );
			copied.displayed = false;
			copied.style!.align = "center";
			copied.style!.flex!.flexBasis = "120px";
			copy.fields.find( f=>f.name=="url" )!.style = new Style( 80 );
			expect( field.displayed ).toBe( true );
			expect( JSON.parse(JSON.stringify(field.style)) ).toEqual( {flex: "0 0 300px", align: "right"} );
			expect( view.fields.find( f=>f.name=="url" )!.style ).toBeUndefined();
			expect( JSON.parse(JSON.stringify(copied.style)) ).toEqual( {flex: "0 0 120px", align: "center"} );
		}
	} );

	//reviews-issue/589-views-draft.md #1:  toJson leaves out the route's name for a column, and fromJson put the schema's in its
	//place - Opc Sessions for Sessions - so a saved view or a draft read back was never alike the route's
	it( "read a saved view back under the route's names for its columns", ()=>{
		const settings = {columns: ["name", {name: "opcSessions", displayName: "Sessions", selection: "count"}], sort: "name"};
		const route = View.fromTable( settings, schema );
		const json = JSON.parse( JSON.stringify(route.toJson(settings)) );
		expect( json.fields.find( ( f:{name:string} )=>f.name=="opcSessions" ).displayName ).toBeUndefined();
		const saved = View.fromJson( json, schema, route );
		expect( saved.fields.find( f=>f.name=="opcSessions" )!.displayName ).toBe( "Sessions" );
		expect( View.alike(saved, route) ).toBe( true );
	} );

	//#588 review #5:  a host keeps its rows, and its page, for a view that shows the rows it did
	it( "take a view renamed, or a copy, as alike, and one sorted, filtered or re-columned as not", ()=>{
		const copy = View.copy( base );
		copy.name = "Mine";
		copy.number = 7;
		expect( View.alike( copy, base ) ).toBe( true );
		const sorted = View.copy( base );
		sorted.sort = [{active: "url", direction: "asc"}];
		const filtered = View.fromConfig( {configColumns: ["name", "url"], sort: [{active: "name", direction: "asc"}], filter: "name:a"}, schema );
		const fewer = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		expect( [sorted, filtered, fewer].map( v=>View.alike( v, base ) ) ).toEqual( [false, false, false] );
		expect( View.alike( base, undefined ) ).toBe( false );
	} );
} );

//#592:  the Fields checklist, a header's menu, its drag and its handle change the view through these
describe( "View's columns", ()=>{
	const things = schemaOf( "Thing", scalar("name"), scalar("slug"), scalar("kind"), scalar("description"), scalar("created", "DateTime") );
	const make = ()=>View.fromTable( {columns: ["name", "slug", {name: "kind", style: new Style({flex: new Flex(120), align: "right"})}], sort: "name"}, things );
	const shown = ( view:View )=>view.fields.filter( f=>f.displayed ).map( f=>f.name );

	//reviews-issue/592-views-fields.md #4
	it( "show a column the view lacks as the route's field has it", ()=>{
		const route = View.fromTable( {columns: ["name", {name: "created", style: new Style( 150 ), chip: {x: "ok"}, selection: "id"}], sort: "name"}, things );
		const view = make();
		view.setDisplayed( "created", true, "Made", route.fields.find( f=>f.name=="created" ) );
		const shown = view.fields.find( f=>f.name=="created" )!;
		expect( [shown.displayed, shown.displayName, shown.flex, shown.chip, shown.selection] ).toEqual( [true, "Created", "0 0 150px", {x: "ok"}, "id"] );
		expect( route.fields.find( f=>f.name=="created" )!.style!.flex ).not.toBe( shown.style!.flex );//its own, not the route's
	} );
	it( "show a column after those shown, one the view lacks under the host's name for it", ()=>{
		const view = make();
		view.setDisplayed( "created", true, "Made" );
		expect( shown(view) ).toEqual( ["name", "slug", "kind", "created"] );
		expect( view.fields.at(-1)!.displayName ).toBe( "Made" );
		expect( view.query(0).text ).toContain( " created " );
		view.setDisplayed( "slug", false );
		view.setDisplayed( "slug", true );//hidden, it comes back last
		expect( shown(view) ).toEqual( ["name", "kind", "created", "slug"] );
		view.setDisplayed( "nothing", true );
		expect( shown(view) ).toEqual( ["name", "kind", "created", "slug"] );
	} );
	it( "hide a column, but never the last one shown", ()=>{
		const view = make();
		view.setDisplayed( "slug", false );
		view.setDisplayed( "kind", false );
		view.setDisplayed( "name", false );
		expect( shown(view) ).toEqual( ["name"] );
	} );
	it( "move a column among those shown, whatever is hidden between them", ()=>{
		const view = make();
		view.setDisplayed( "slug", false );
		view.moveField( "kind", 0 );
		expect( shown(view) ).toEqual( ["kind", "name"] );
		view.moveField( "kind", 1 );
		expect( shown(view) ).toEqual( ["name", "kind"] );
		view.moveField( "kind", 9 );//held at the last place
		view.moveField( "name", -1 );
		view.moveField( "slug", 0 );//not shown
		expect( shown(view) ).toEqual( ["name", "kind"] );
	} );
	it( "write a width as a fixed basis, held at the floor, the alignment kept", ()=>{
		const view = make();
		view.setWidth( "kind", 201.4 );
		expect( view.fields.find( f=>f.name=="kind" )!.flex ).toBe( "0 0 201px" );
		expect( view.fields.find( f=>f.name=="kind" )!.style!.align ).toBe( "right" );
		view.setWidth( "name", 10 );
		expect( view.fields.find( f=>f.name=="name" )!.flex ).toBe( `0 0 ${View.minWidth}px` );
		expect( View.minWidth ).toBe( 64 );
	} );
	//reviews-issue/592-views-fields.md #3, #15
	it( "never narrow a column under the width it had where that is under the floor, nor widen it there", ()=>{
		expect( [View.widthOf( 13, 45 ), View.widthOf( 50, 45 ), View.widthOf( 30, 100 ), View.widthOf( 30 )] ).toEqual( [45, 50, 64, 64] );
		const view = make();
		view.setWidth( "name", 13, 45 );
		expect( view.fields.find( f=>f.name=="name" )!.flex ).toBe( "0 0 45px" );
		expect( [View.flexOf( 45 ), new Flex( "0 0 45px" ).px, new Flex( "1" ).px] ).toEqual( ["0 0 45px", 45, undefined] );
	} );
	it( "save a width with the view, and read it back", ()=>{
		const view = make();
		view.setWidth( "name", 180 );
		const back = View.fromJson( JSON.parse( JSON.stringify(view.toJson(undefined)) ), things, make() );
		expect( back.fields.find( f=>f.name=="name" )!.flex ).toBe( "0 0 180px" );
		expect( View.alike( back, view ) ).toBe( true );
		expect( View.alike( view, make() ) ).toBe( false );
	} );
	it( "restore the route's width, or none where the route has none", ()=>{
		const route = make(), view = make();
		view.setWidth( "kind", 300 );
		view.setWidth( "name", 300 );
		view.restoreWidth( "kind", route );
		view.restoreWidth( "name", route );
		expect( view.fields.find( f=>f.name=="kind" )!.flex ).toBe( "0 0 120px" );
		expect( view.fields.find( f=>f.name=="name" )!.style ).toBeUndefined();
		expect( View.alike( view, route ) ).toBe( true );
	} );
	it( "pin the name, or the first column shown where the view shows none", ()=>{
		const view = make();
		view.moveField( "name", 2 );
		expect( view.pinned ).toBe( "name" );
		view.setDisplayed( "name", false );
		expect( view.pinned ).toBe( "slug" );
	} );
	it( "take the rows read as the view's where it only hides, moves or resizes a column, and not where it shows or filters one", ()=>{
		const read = make();
		const change = ( edit:( view:View )=>void )=>{ const view = View.copy( read ); edit( view ); return View.answers( read, view ); };
		expect( change( ()=>{} ) ).toBe( true );
		expect( change( v=>v.setDisplayed("slug", false) ) ).toBe( true );
		expect( change( v=>v.moveField("kind", 0) ) ).toBe( true );
		expect( change( v=>v.setWidth("name", 200) ) ).toBe( true );
		expect( change( v=>{ v.filter = "nothing:1"; } ) ).toBe( true );//a term the box cannot read is not sent
		expect( change( v=>v.setDisplayed("created", true) ) ).toBe( false );
		expect( change( v=>{ v.filter = "kind:a"; } ) ).toBe( false );
		expect( change( v=>{ v.sort = [{active: "slug", direction: "asc"}]; } ) ).toBe( false );
		expect( change( v=>{ v.limit = 50; } ) ).toBe( false );
		expect( View.answers( undefined, read ) ).toBe( false );
	} );
} );

//#593:  the header menu's sorts and the view menu's `Sort by` (views spec *Sort*)
describe( "View's sort", ()=>{
	const things = schemaOf( "Thing", scalar("name"), scalar("slug"), scalar("kind"), object("owner", "Owner") );
	const make = ()=>View.fromTable( {columns: ["name", "slug", "owner"], sort: "name"}, things );
	const sorted = ( view:View )=>view.sort.map( s=>`${s.active} ${s.direction}` );

	it( "reaches the query in its order", ()=>{
		const view = make();
		view.toggleSort( "kind" );
		view.toggleSort( "slug" );
		view.toggleSort( "kind" );//the view sorting by it:  the other way, where it stands
		expect( sorted(view) ).toEqual( ["name asc", "kind desc", "slug asc"] );
		expect( view.query(0).text ).toContain( 'orderBy:[{name:"asc"},{kind:"desc"},{slug:"asc"}]' );
		view.moveSort( "slug", 0 );
		expect( view.query(0).text ).toContain( 'orderBy:[{slug:"asc"},{name:"asc"},{kind:"desc"}]' );
		view.sort = [];
		expect( view.query(0).text ).not.toContain( "orderBy" );
	} );
	it( "takes a header menu's direction where the column stands, and a column it lacks first", ()=>{
		const view = make();
		view.toggleSort( "slug" );
		view.setSort( "slug", "desc" );
		expect( sorted(view) ).toEqual( ["name asc", "slug desc"] );
		view.setSort( "kind", "desc" );
		expect( sorted(view) ).toEqual( ["kind desc", "name asc", "slug desc"] );
		view.sortFirst( {active: "slug", direction: "asc"} );//a header's click
		expect( sorted(view) ).toEqual( ["slug asc", "kind desc", "name asc"] );
	} );
	it( "moves a sort within the sort, held at its ends", ()=>{
		const view = make();
		view.toggleSort( "slug" );
		view.toggleSort( "kind" );
		view.moveSort( "name", 9 );
		expect( sorted(view) ).toEqual( ["slug asc", "kind asc", "name asc"] );
		view.moveSort( "kind", -1 );
		view.moveSort( "owner", 0 );//not in it
		expect( sorted(view) ).toEqual( ["kind asc", "slug asc", "name asc"] );
	} );
	it( "refuses a grafted object column, and one the list does not have", ()=>{
		const view = make();
		expect( ["name", "kind", "owner", "nothing"].map( c=>view.sortable(c) ) ).toEqual( [true, true, false, false] );//kind:  the schema's, the view not holding it
	} );
	//reviews-issue/593-views-sort.md #2:  OBJECT alone was refused, so a list reached orderBy, which the server refuses
	it( "refuses a list column, a flags column as the introspection gives it among them, and a union", ()=>{
		const lists = schemaOf( "Thing", scalar("name"),
			{name: "rights", type: {kind: "NON_NULL", name: null, ofType: {kind: "LIST", name: "Rights"}}},//a flags column, its list not wrapping its type
			{name: "tags", type: {kind: "LIST", name: null, ofType: {kind: "SCALAR", name: "String"}}},
			{name: "members", type: {kind: "LIST", name: null, ofType: {kind: "UNION", name: "Identity"}}},
			{name: "identity", type: {kind: "UNION", name: "Identity"}} );
		const view = View.fromTable( {columns: ["name"], sort: "name"}, lists );
		expect( ["name", "rights", "tags", "members", "identity"].map( c=>view.sortable(c) ) ).toEqual( [true, false, false, false, false] );
	} );
} );
//#595:  the slice column is the view's, saved with it, and the value the rows are narrowed to is the browser's
describe( "View slice", ()=>{
	const users = schemaOf( "User", {name: "id", type: {kind: "NON_NULL", name: null, ofType: {kind: "SCALAR", name: "ID"}}}, scalar("name"), {name: "provider", type: {kind: "ENUM", name: "Provider"}}, scalar("isGroup", "Boolean"),
		scalar("logins", "UInt"), scalar("deleted", "DateTime"), object("owner", "Owner"), {name: "tags", type: {kind: "LIST", name: null, ofType: {kind: "SCALAR", name: "Int"}}}, scalar("rate", "Float") );
	const view = ( settings:object={} )=>View.fromTable( {columns: ["name", "provider"], sort: "name", ...settings}, users );
	//reviews-issue/595-views-slice-panel.md #6:  a Float was offered, which the server's `in:[…]` need not match
	it( "slices by a number, a boolean or an enum, never an ID, text, a date, an object, a list or a Float", ()=>{
		const v = view();
		expect( ["id", "name", "provider", "isGroup", "logins", "deleted", "owner", "tags", "rate"].map( c=>v.sliceable(c) ) ).toEqual( [false, false, true, true, true, false, false, false, false] );
		expect( v.sliceChoices({name: "Name", provider: "Provider", logins: "Logins"}) ).toEqual( [{name: "provider", label: "Provider"}, {name: "logins", label: "Logins"}] );
	} );
	it( "counts by the column under the view's filter, with none of its sort, its page or its slice", ()=>{
		const v = view( {sliceBy: "provider"} );
		v.filter = "logins:>3";
		v.slice = "Key";
		const q = v.sliceQuery()!;
		expect( q.text ).toBe( 'users(groupBy:"provider",logins:{gt:$logins_gt},deleted:$deleted){ provider count }' );
		expect( q.vars ).toEqual( {logins_gt: 3, deleted: null} );
		expect( view().sliceQuery() ).toBeUndefined();
	} );
	it( "narrows the rows to the slice's value, with the filter's terms on the column, and to the rows with none for null", ()=>{
		const v = view( {sliceBy: "provider"} );
		v.slice = "Key";
		expect( v.query( 0 ) ).toMatchObject( {vars: {provider: ["Key"]}} );
		v.filter = "provider:Key,Google";
		expect( v.query( 0 ).vars["provider"] ).toEqual( ["Key"] );
		v.filter = "";
		v.slice = null;
		expect( v.query( 0 ).vars["provider"] ).toEqual( [null] );
		expect( View.sameRows( view({sliceBy: "provider"}), v ) ).toBe( false );
	} );
	it( "saves the column and never the value, and drops a saved column the schema no longer slices by", ()=>{
		const v = view( {sliceBy: "provider"} );
		v.slice = "Key";
		const json = v.toJson( undefined );
		expect( json ).toMatchObject( {sliceBy: "provider"} );
		expect( JSON.stringify(json) ).not.toContain( "Key" );
		expect( View.fromJson( json as unknown as ViewSerializedArgs, users ).sliceBy ).toBe( "provider" );
		expect( View.fromJson( {...json, sliceBy: "name"} as unknown as ViewSerializedArgs, users ).sliceBy ).toBeUndefined();
		expect( View.alike( v, view() ) ).toBe( false );//a change to the view, so a draft
		expect( ()=>view( {sliceBy: "name"} ) ).toThrow();
	} );
} );
//#596:  the group column and the sums are the view's, saved with it, and the group column is its rows' first sort
describe( "View group", ()=>{
	const users = schemaOf( "User", {name: "id", type: {kind: "NON_NULL", name: null, ofType: {kind: "SCALAR", name: "ID"}}}, scalar("name"), {name: "provider", type: {kind: "ENUM", name: "Provider"}}, scalar("isGroup", "Boolean"),
		scalar("logins", "UInt"), scalar("deleted", "DateTime"), {name: "tags", type: {kind: "LIST", name: null, ofType: {kind: "SCALAR", name: "Int"}}}, scalar("rate", "Float") );
	const view = ( settings:object={} )=>View.fromTable( {columns: ["name", "provider"], sort: "name", ...settings}, users );
	it( "sorts by the group column first, ascending or as the view sorts by it, ahead of the view's own", ()=>{
		const v = view( {groupBy: "provider"} );
		expect( v.ordered ).toEqual( [{active: "provider", direction: "asc"}, {active: "name", direction: "asc"}] );
		expect( v.query( 0 ).text ).toContain( 'orderBy:[{provider:"asc"},{name:"asc"}]' );
		v.sort = [{active: "name", direction: "asc"}, {active: "provider", direction: "desc"}];
		expect( v.ordered ).toEqual( [{active: "provider", direction: "desc"}, {active: "name", direction: "asc"}] );
		expect( [v.sort.length, view().ordered] ).toEqual( [2, [{active: "name", direction: "asc"}]] );//the view's sort is as the user left it
		expect( View.sameRows( view(), view({groupBy: "provider"}) ) ).toBe( false );//another order:  read again
	} );
	it( "counts a group over the whole list:  under the view's filter, with its slice's value and without", ()=>{
		const v = view( {groupBy: "isGroup", sliceBy: "provider"} );
		v.filter = "logins:>3";
		expect( v.groupQuery() ).toEqual( {text: 'users(groupBy:"isGroup",logins:{gt:$logins_gt},deleted:$deleted){ isGroup count }', vars: {logins_gt: 3, deleted: null}} );
		v.slice = "Key";
		expect( v.groupQuery() ).toEqual( {text: 'users(groupBy:"isGroup",logins:{gt:$logins_gt},provider:$provider,deleted:$deleted){ isGroup count }', vars: {logins_gt: 3, provider: ["Key"], deleted: null}} );
		expect( view().groupQuery() ).toBeUndefined();
	} );
	it( "sums a number, never an ID, text, a boolean, a date or a list", ()=>{
		const v = view();
		expect( ["id", "name", "provider", "isGroup", "logins", "deleted", "tags", "rate"].map( c=>v.summable(c) ) ).toEqual( [false, false, false, false, true, false, false, true] );
		expect( v.sumChoices({id: "ID", name: "Name", logins: "Logins", rate: "Rate"}) ).toEqual( [{name: "logins", label: "Logins"}, {name: "rate", label: "Rate"}] );
	} );
	it( "reads the group column and the sums with the rows though the view hides them", ()=>{
		const v = view();
		expect( v.query( 0 ).text ).not.toContain( "logins" );
		v.setGroupBy( "isGroup" );
		v.toggleSum( "logins" );
		expect( v.query( 0 ).text ).toContain( "{ id name provider isGroup logins }" );
		expect( v.fields.filter( f=>f.displayed ).map( f=>f.name ) ).toEqual( ["name", "provider"] );
		expect( View.answers( view(), v ) ).toBe( false );//columns the rows read lack
		v.toggleSum( "logins" );
		v.setGroupBy( undefined );
		expect( [v.sums, v.query( 0 ).text.includes( "logins" ), View.alike( v, view() )] ).toEqual( [[], false, true] );
	} );
	it( "says what the rows are grouped by, under the headers' names", ()=>{
		const v = view( {columns: ["name", {name: "provider", displayName: "Sign-in", chip: {Key: "ok"}}], groupBy: "provider", sums: ["logins"]} );
		const totals = [{value: "Key", count: 3}];
		expect( v.grouping( {logins: "Logins"}, undefined, totals ) ).toEqual( {by: "provider", label: "Sign-in", chip: {Key: "ok"}, sums: [{name: "logins", label: "Logins"}], totals} );
		expect( view().grouping( {} ) ).toBeUndefined();
	} );
	//reviews-issue/596-views-group-by.md #3:  a column the view lacked joined its fields as the schema has it - no chip, width or
	//name - and stayed there, so a Save wrote it so and the Fields checklist showed it bare
	it( "never makes a column it lacks one of its fields:  groups under the route's chip, and shows it later as the route has it", ()=>{
		const settings:TableSettings = {columns: ["name", {name: "provider", displayName: "Sign-in", style: new Style( 100 ), chip: {Key: "ok"}}], sort: "name"};
		const route = View.fromTable( settings, users );
		const saved = View.fromJson( {...route.toJson( settings ), fields: [{name: "name"}]} as unknown as ViewSerializedArgs, users, route );
		const json = JSON.stringify( saved.toJson(settings) ), lacks = ()=>!saved.fields.some( f=>f.name=="provider" || f.name=="logins" );
		saved.setGroupBy( "provider" );
		saved.toggleSum( "logins" );
		expect( [lacks(), saved.query( 0 ).text.includes( " provider logins }" ), saved.grouping( {provider: "Provider"}, route )] ).toEqual( [true, true, {by: "provider", label: "Provider", chip: {Key: "ok"}, sums: [{name: "logins", label: "Logins"}], totals: undefined}] );
		saved.toggleSum( "logins" );
		saved.setGroupBy( undefined );
		expect( [lacks(), JSON.stringify( saved.toJson(settings) )] ).toEqual( [true, json] );//no draft left by a group taken off
		saved.setGroupBy( "provider" );
		saved.setDisplayed( "provider", true, "Provider", route.fields.find( f=>f.name=="provider" ) );
		const shown = saved.fields.find( f=>f.name=="provider" )!;
		expect( [shown.displayed, shown.displayName, shown.flex, shown.chip] ).toEqual( [true, "Sign-in", "0 0 100px", {Key: "ok"}] );
	} );
	it( "saves the column and the sums, and drops a saved one the schema no longer groups by or sums", ()=>{
		const v = view( {groupBy: "provider", sums: ["logins"]} );
		const json = v.toJson( undefined );
		expect( json ).toMatchObject( {groupBy: "provider", sums: ["logins"]} );
		expect( view().toJson( undefined ) ).toMatchObject( {groupBy: undefined, sums: undefined} );
		const saved = View.fromJson( json as unknown as ViewSerializedArgs, users );
		expect( [saved.groupBy, saved.sums, saved.query( 0 ).text.includes( "logins" )] ).toEqual( ["provider", ["logins"], true] );
		const stale = View.fromJson( {...json, groupBy: "name", sums: ["name", "gone"]} as unknown as ViewSerializedArgs, users );
		expect( [stale.groupBy, stale.sums] ).toEqual( [undefined, []] );
		expect( [View.copy( v ).groupBy, View.copy( v ).sums, View.alike( v, view() )] ).toEqual( ["provider", ["logins"], false] );
		expect( ()=>view( {groupBy: "name"} ) ).toThrow();
		expect( ()=>view( {sums: ["provider"]} ) ).toThrow();
	} );
} );