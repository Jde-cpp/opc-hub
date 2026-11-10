import { Flex, Style, View, ViewField, type ViewFieldSettings, type ViewSerializedArgs } from './view';
import { scalar, schemaOf, serverConnectionSchema } from 'jde-framework/testing';

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