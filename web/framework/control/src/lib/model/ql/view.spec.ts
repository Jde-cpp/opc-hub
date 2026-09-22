import { describe, it, expect } from 'vitest';
import { Flex, Operator, Style, View } from './view';
import { TableSchema } from './schema/table-schema';

//Mirrors the gateway's ServerConnection: introspected DB columns plus the grafted opcSessions OBJECT (config/introspection/serverConnection.jsonnet).
const schema = new TableSchema( {
	name: "ServerConnection",
	fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "url", type: { kind: "SCALAR", name: "String" } },
		{ name: "deleted", type: { kind: "SCALAR", name: "DateTime" } },
		{ name: "description", type: { kind: "SCALAR", name: "String" } },
		{ name: "provider", type: { kind: "OBJECT", name: "Provider" } },
		{ name: "opcSessions", type: { kind: "OBJECT", name: "OpcSessions" } }
	]
} );

describe( "View.query", ()=>{
	it( "emits an explicit sub-selection for an OBJECT column", ()=>{
		const view = new View( {columns: ["name", "url", {name:"opcSessions", displayName:"Sessions", selection:"count"}], sort: "name"}, schema );
		const query = view.query( false, 0 );
		expect( query.text ).toContain( "opcSessions{count}" );
		expect( query.text ).toContain( "serverConnections(" );
		expect( query.text ).not.toContain( "opcSessions " );//never the bare name - the server rejects a selection-less object field.
	} );
	it( "defaults an OBJECT column to the {id name} convention", ()=>{
		const view = new View( {columns: ["name", "provider"], sort: "name"}, schema );
		expect( view.query(false, 0).text ).toContain( "provider{id name}" );
	} );
	it( "round-trips selection through toJson", ()=>{
		const view = new View( {columns: [{name:"opcSessions", selection:"count"}], sort: "name"}, schema );
		const field = view.fields.find( f=>f.name=="opcSessions" )!;
		expect( field.toJson().selection ).toBe( "count" );
	} );
} );

//reviews/m3-closing.md #16:  /access/resources is a live-toggle page - `deleted` is its Enforced switch - so it always queries with
//showDeleted on, and query() spliced out every `deleted:` filter whenever showDeleted was on.  "Enforced only" (deleted is null)
//and "not enforced" (not null) went out as no filter at all.  And a lone "null" under a comparison operator - a DateTime column
//offers only < and > - was sent as a timestamp, `{gt:"<null>"}`, rather than as `is null`.
describe( "View.query on a live-toggle column's filter", ()=>{
	const liveToggle = { enable: "Enforce", disable: "Stop enforcing" };
	const view = ( settings:{liveToggle?:typeof liveToggle}, value:any[], operator=Operator.Greater, column="deleted" )=>{
		const v = new View( {columns: ["name", {name: "deleted", displayName: "Enforced", ...settings}], sort: "name"}, schema );
		v.fieldFilters.push( {field: schema.fields.find( f=>f.name==column )!, filter: {operator, value}} );
		return v.query( true, 0 );
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
	it( 'still lets Show deleted win where deleted is only the trash can', ()=>{//the checkbox is the way out there
		const q = view( {}, ["<null>"] );
		expect( q.text ).not.toContain( "deleted:" );
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
		const view = new View( {columns: [{name:"opcSessions", selection:"count", style: new Style({align:"right"})}], sort: "name"}, schema );
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
		const view = new View( serialized(), schema, [] );
		const names = view.fields.map( f=>f.name );
		expect( names ).toContain( "name" );
		expect( names ).not.toContain( "groupings" );
	} );
	it( "drops a filter on a column the schema no longer has", ()=>{
		const view = new View( serialized(), schema, [] );
		expect( view.fieldFilters ).toHaveLength( 0 );
	} );
	it( "still builds a query afterwards", ()=>{
		const view = new View( serialized(), schema, [] );
		const query = view.query( false, 0 );
		expect( query.text ).toContain( "serverConnections(" );
		expect( query.text ).not.toContain( "groupings" );
	} );
	it( "keeps a filter whose column survives", ()=>{
		const args = serialized();
		args.filters = [ {name: "url", filter: {operator: Operator.In, value: ["a"]}} ];
		const view = new View( args, schema, [] );
		expect( view.fieldFilters.map(ff=>ff.field.name) ).toEqual( ["url"] );
	} );
} );
