import { Flex, Operator, Style, View, ViewField, type ViewFieldSettings, type ViewSerializedArgs } from './view';
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

//reviews/m3-closing.md #16:  /access/resources is a live-toggle page - `deleted` is its Enforced switch - so it always queries with
//showDeleted on, and query() spliced out every `deleted:` filter whenever showDeleted was on.  "Enforced only" (deleted is null)
//and "not enforced" (not null) went out as no filter at all.  And a lone "null" under a comparison operator - a DateTime column
//offers only < and > - was sent as a timestamp, `{gt:"<null>"}`, rather than as `is null`.
describe( "View.query on a live-toggle column's filter", ()=>{
	const liveToggle = { enable: "Enforce", disable: "Stop enforcing" };
	const view = ( settings:{liveToggle?:typeof liveToggle}, value:any[], operator=Operator.Greater, column="deleted" )=>{
		const v = View.fromTable( {columns: ["name", {name: "deleted", displayName: "Enforced", ...settings}], sort: "name"}, schema );
		v.fieldFilters.push( {field: schema.fields.find( f=>f.name==column )!, filter: {operator, value}} );
		return v.query( 0 );
	};
	it( 'sends "null" - the enforced rows - as is null', ()=>{
		const q = view( {liveToggle}, ["<null>"] );
		expect( q.text ).toContain( "deleted:$deleted" );
		expect( q.vars["deleted"] ).toEqual( [null] );
	} );
	it( 'sends "not null"', ()=>{
		expect( view({liveToggle}, ["<not null>"]).text ).toContain( 'deleted:{"ne":null}' );
	} );
	it( 'sends a date', ()=>{
		const q = view( {liveToggle}, [new Date("2026-09-01T00:00:00Z")] );
		expect( q.text ).toContain( "deleted:{gt:$deleted}" );
		expect( q.vars["deleted"] ).toBeDefined();
	} );
	it( 'keeps a filter on deleted where it is only the trash can', ()=>{//the Deleted view is one
		const q = view( {}, ["<not null>"] );
		expect( q.text ).toContain( 'deleted:{"ne":null}' );
		expect( q.text ).not.toContain( "deleted:$deleted" );
	} );
	it( 'leaves NotIn null as not null', ()=>{
		const q = view( {}, ["<null>"], Operator.NotIn, "url" );
		expect( q.text ).toContain( "url:{nin:$url}" );
		expect( q.vars["url"] ).toEqual( [null] );
	} );
	it( 'sends a lone "null" under a comparison as is null on any nullable column', ()=>{
		const q = view( {}, ["<null>"], Operator.Less, "url" );
		expect( q.text ).toContain( "url:$url" );
		expect( q.vars["url"] ).toEqual( [null] );
	} );
	//a saved view from before ql-list-settings-filter's fix can still hold what the dialog used to build
	it( 'reads a doubled marker as the marker', ()=>{
		expect( view({liveToggle}, ["<null>", "<null>"]).vars["deleted"] ).toEqual( [null] );
		expect( view({liveToggle}, ["<not null>", "<not null>"]).text ).toContain( 'deleted:{"ne":null}' );
	} );
	it( 'never sends a comparison without a value', ()=>{
		expect( view({liveToggle}, [undefined]).text ).not.toContain( "deleted:" );
		expect( view({liveToggle}, [null]).text ).not.toContain( "deleted:" );
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
		filters: [ {name: "groupings", filter: {operator: Operator.In, value: ["a"]}} ],
		showSelector: false,
		sort: [ {active: "name", direction: "asc"} ]
	});
	it( "drops a displayed column the schema no longer has", ()=>{
		const view = View.fromJson( serialized(), schema );
		const names = view.fields.map( f=>f.name );
		expect( names ).toContain( "name" );
		expect( names ).not.toContain( "groupings" );
	} );
	it( "drops a filter on a column the schema no longer has", ()=>{
		const view = View.fromJson( serialized(), schema );
		expect( view.fieldFilters ).toHaveLength( 0 );
	} );
	it( "still builds a query afterwards", ()=>{
		const view = View.fromJson( serialized(), schema );
		const query = view.query( 0 );
		expect( query.text ).toContain( "serverConnections(" );
		expect( query.text ).not.toContain( "groupings" );
	} );
	it( "keeps a filter whose column survives", ()=>{
		const args = serialized();
		args.filters = [ {name: "url", filter: {operator: Operator.In, value: ["a"]}} ];
		const view = View.fromJson( args, schema );
		expect( view.fieldFilters.map(ff=>ff.field.name) ).toEqual( ["url"] );
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

	it( "copies the filters, not the copy's source's", ()=>{
		const route = View.fromConfig( {configColumns: ["name"], sort: [], filters: [{name: "name", value: ["a"]}]}, schema );
		const copy = View.copy( route );
		copy.fieldFilters[0].filter.value.push( "b" );
		expect( route.fieldFilters[0].filter.value ).toEqual( ["a"] );
	} );
} );