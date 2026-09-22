import { describe, it, expect } from 'vitest';
import { Operator, View } from '../model/ql/view';
import { TableSchema } from '../model/ql/schema/table-schema';
import { ListRoute, QLListData, QLListResolver, TableSettings } from './ql-list-resolver';
import { PageProfile } from '../pages/graphql/model/page-settings';

const schema = new TableSchema( {
	name: "Thing",
	fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "kind", type: { kind: "SCALAR", name: "String" } },
		{ name: "deleted", type: { kind: "SCALAR", name: "DateTime" } },
		{ name: "description", type: { kind: "SCALAR", name: "String" } }
	]
} );
const displayed = ( v:View )=>v.fields.filter( f=>f.displayed ).map( f=>f.name );

//A route declares its list page's system views in TableSettings:  the default one from columns/sort under `viewName`, and
//`views` beside it - each a named, optionally filtered variant that inherits whatever it leaves unset.
describe( 'QLListResolver.systemViews', ()=>{
	it( 'is just the unnamed default view when the route declares none', ()=>{
		const views = QLListResolver.systemViews( schema, {columns: ["name", "description"]} );
		expect( views.map(v=>v.name) ).toEqual( [undefined] );
		expect( views[0].isSystem ).toBe( true );
	} );

	it( 'names the default view and appends the declared ones, all as system views', ()=>{
		const settings:TableSettings = { viewName: "all", columns: ["name", "kind", "description"], views: [
			{ name: "Kinds", columns: ["name", "kind"], filters: [{name: "kind", value: ["a", "b"]}] },
			{ name: "Newest", sort: "slug", filters: [{name: "kind", operator: Operator.NotIn, value: ["<null>"]}] }
		] };
		const views = QLListResolver.systemViews( schema, settings );
		expect( views.map(v=>v.name) ).toEqual( ["all", "Kinds", "Newest"] );
		expect( views.every(v=>v.isSystem) ).toBe( true );
	} );

	it( 'gives a declared view the default columns and sort unless it sets its own', ()=>{
		const [all, kinds, newest] = QLListResolver.systemViews( schema, { columns: ["name", "kind", "description"], views: [
			{ name: "Kinds", columns: ["name", "kind"] },
			{ name: "Newest", sort: "slug" }
		] } );
		expect( displayed(kinds) ).toEqual( ["name", "kind"] );
		expect( kinds.sort ).toEqual( all.sort );
		expect( kinds.sort ).not.toBe( all.sort );//its own array - a header sort on one view must not reach the other
		expect( displayed(newest) ).toEqual( displayed(all) );
		expect( newest.sort ).toEqual( [{active: "slug", direction: "asc"}] );
	} );

	it( "sorts the default view by the route's own sort, multi-column and all", ()=>{
		const sort = [{active: "kind", direction: "asc" as const}, {active: "name", direction: "asc" as const}];
		const [all] = QLListResolver.systemViews( schema, { columns: ["kind", "name", "description"], sort } );
		expect( all.sort ).toEqual( sort );
		expect( all.query(false, 0).text ).toContain( 'orderBy:[{kind:"asc"},{name:"asc"}]' );
	} );

	it( 'falls back to an ascending name sort when the route declares none', ()=>{
		const [all] = QLListResolver.systemViews( schema, { columns: ["name", "description"] } );
		expect( all.sort ).toEqual( [{active: "name", direction: "asc"}] );
	} );

	//A live-toggle column switches the DELETED rows, so its page has to query them.  On `resources` every row ships deleted
	//(unenforced), so without this the page opens empty with all the work hidden behind a checkbox called "Show deleted".
	it( 'reports a live-toggle column wherever a route declares one', ()=>{
		const toggle = {name:"deleted", displayName:"Enforced", liveToggle:{enable:"Enforce", disable:"Stop enforcing"}};
		expect( QLListResolver.hasLiveToggle({ columns: ["name", toggle] }) ).toBe( true );
		expect( QLListResolver.hasLiveToggle({ columns: ["name"], views: [{name:"All", columns: ["name", toggle]}] }) ).toBe( true );
		expect( QLListResolver.hasLiveToggle({ columns: ["name", "deleted"] }) ).toBe( false );
		expect( QLListResolver.hasLiveToggle({}) ).toBe( false );
	} );

	it( 'turns a declared filter into the query the settings panel would have built', ()=>{
		const [, kinds] = QLListResolver.systemViews( schema, { columns: ["name"], views: [
			{ name: "Kinds", filters: [{name: "kind", value: ["a", "<null>"]}] }
		] } );
		expect( kinds.fieldFilters ).toHaveLength( 1 );
		expect( kinds.fieldFilters[0].field.name ).toBe( "kind" );
		expect( kinds.fieldFilters[0].filter.operator ).toBe( Operator.In );
		const q = kinds.query( false, 0 );
		expect( q.text ).toContain( "kind:$kind" );
		expect( q.vars["kind"] ).toEqual( ["a", null] );//"<null>" leaves as a JSON null, as the panel's does
	} );

	//The default view can be the filtered one - resources opens on its table rows and keeps an unfiltered 'All' beside it.
	it( 'filters the default view when the route declares filters, and not the views beside it', ()=>{
		const [tables, all] = QLListResolver.systemViews( schema, { viewName: "Tables", columns: ["name"], filters: [{name: "kind", value: ["<null>"]}], views: [{name: "All"}] } );
		expect( tables.name ).toBe( "Tables" );
		expect( tables.query(false, 0).vars["kind"] ).toEqual( [null] );
		expect( all.fieldFilters ).toHaveLength( 0 );
		expect( displayed(all) ).toEqual( displayed(tables) );
	} );

	it( 'refuses a filter on a column the schema does not have', ()=>{
		expect( ()=>QLListResolver.systemViews(schema, { columns: ["name"], views: [{name: "Bad", filters: [{name: "nope", value: [1]}]}] }) ).toThrow( /nope/ );
	} );
} );

//MVP first-run:  what a list says when the query returns nothing.  The route's own words win; otherwise the collection's
//name, and a pointer at Add only where the route offers one.
describe( 'QLListResolver.emptyState', ()=>{
	it( 'names the collection and points at Add by default', ()=>{
		expect( QLListResolver.emptyState(new ListRoute("users")) ).toEqual( {title: "No users yet.", detail: "Use Add to create the first one.", icon: "inbox"} );
	} );
	it( 'drops the Add pointer where the route offers no Add', ()=>{
		expect( QLListResolver.emptyState(new ListRoute({path: "resources", data: {tableSettings: {canAdd: false}} as any})).detail ).toBe( "" );
	} );
	it( "takes the route's own words", ()=>{
		const own = QLListResolver.emptyState( new ListRoute({path: "roles", data: {tableSettings: {empty: {title: "Nothing.", detail: "Yet."}}} as any}) );
		expect( own ).toEqual( {title: "Nothing.", detail: "Yet.", icon: "inbox"} );
	} );
	//reviews/m3-closing.md #24:  the Add pointer is its own sentence, so a list without Add can leave it off
	it( "appends the route's Add sentence only where Add is shown", ()=>{
		const route = ( tableSettings:object )=>new ListRoute( {path: "groups", data: {tableSettings}} as any );
		const empty = {title: "No groups yet.", detail: "A group collects users.", add: "Use Add to create one."};
		expect( QLListResolver.emptyState(route({empty})).detail ).toBe( "A group collects users.  Use Add to create one." );
		expect( QLListResolver.emptyState(route({empty}), {selector: true}).detail ).toBe( "A group collects users." );
		expect( QLListResolver.emptyState(route({empty, canAdd: false})).detail ).toBe( "A group collects users." );
		expect( QLListResolver.emptyState(route({empty: {add: "Use Add to connect one."}})).detail ).toBe( "Use Add to connect one." );
	} );
	it( 'says a narrowed list is narrowed, not empty', ()=>{
		const route = new ListRoute( {path: "roles", data: {tableSettings: {empty: {title: "No roles yet.", detail: "Seeded."}}}} as any );
		expect( QLListResolver.emptyState(route, {filtered: true}) ).toEqual( {title: "No roles match this view.", detail: "", icon: "filter_alt_off"} );
		expect( QLListResolver.emptyState(route, {excluded: true, selector: true}) ).toEqual( {title: "No other roles.", detail: "", icon: "inbox"} );
	} );
} );

//A refused rows query used to reject the resolve, which the router turned into a NavigationError nobody saw.  It is the
//page's own state now, so a user without Read on the collection lands on the page and reads why.
describe( 'QLListResolver.loadOrFail', ()=>{
	it( "turns a refused rows query into the page's error state with no rows", async ()=>{
		const refused = new Error( "no" );
		const ql = { query: async ()=>{ throw refused; } } as any;
		const profile = new PageProfile();
		profile.views = [ new View({columns: ["name"], sort: "name"}, schema) ];
		profile.showDeleted = false;
		const data = { schema, profile, routing: new ListRoute("things"), columns: {}, pageSettings: {} } as unknown as QLListData;
		const y = await QLListResolver.loadOrFail( ql, data, null );
		expect( y.error ).toBe( refused );
		expect( y.results ).toEqual( {[schema.collectionName]: []} );
		expect( y.schema ).toBe( schema );
	} );
} );
