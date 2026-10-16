import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { Sort } from '@angular/material/sort';
import { provideRouter, Router } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { ProfileStore, RouteStore } from 'jde-spa';
import { SnackbarService } from '../shared/snackbar/snackbar-service';
import { IGRAPHQL } from './graphql';
import { Operator, View } from '../model/ql/view';
import { TableSchema } from '../model/ql/schema/table-schema';
import { ListRoute, QLListData, QLListResolver, TableSettings } from './ql-list-resolver';
import { PageProfile } from '../pages/graphql/model/page-settings';

const fields = [
	{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
	{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
	{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
	{ name: "kind", type: { kind: "SCALAR", name: "String" } },
	{ name: "description", type: { kind: "SCALAR", name: "String" } }
];
const schema = new TableSchema( { name: "Thing", fields } );//no deleted column, so no Deleted view to step around
const deletable = new TableSchema( { name: "Thing", fields: [...fields, { name: "deleted", type: { kind: "SCALAR", name: "DateTime" } }] } );
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
		expect( all.query( 0 ).text ).toContain( 'orderBy:[{kind:"asc"},{name:"asc"}]' );
	} );

	it( 'falls back to an ascending name sort when the route declares none', ()=>{
		const [all] = QLListResolver.systemViews( schema, { columns: ["name", "description"] } );
		expect( all.sort ).toEqual( [{active: "name", direction: "asc"}] );
	} );

	//The way to deleted rows, now there is no Show deleted:  a last system view, the default's columns and filters plus deleted.
	it( 'ends with a Deleted view where the collection has a deleted column', ()=>{
		const views = QLListResolver.systemViews( deletable, { columns: ["name"], filters: [{name: "kind", value: ["a"]}], views: [{name: "Kinds"}] } );
		expect( views.map(v=>v.name) ).toEqual( [undefined, "Kinds", "Deleted"] );
		const deleted = views[2];
		expect( deleted.isSystem ).toBe( true );
		expect( QLListResolver.isDeletedView(deleted) ).toBe( true );
		expect( displayed(deleted) ).toEqual( ["name", "deleted"] );
		const q = deleted.query( 0 );
		expect( q.text ).toContain( 'deleted:{"ne":null}' );
		expect( q.vars["kind"] ).toEqual( ["a"] );
		expect( views[0].fields.find(f=>f.name=="deleted")!.displayed ).toBe( false );//on a copy:  the default view keeps it hidden
	} );

	it( 'adds no Deleted view where deleted is a live toggle, or missing', ()=>{
		const toggle = {name:"deleted", displayName:"Enforced", liveToggle:{enable:"Enforce", disable:"Stop enforcing"}};
		expect( QLListResolver.systemViews(deletable, { columns: ["name", toggle] }).map(v=>v.name) ).toEqual( [undefined] );
		expect( QLListResolver.systemViews(schema, { columns: ["name"] }).map(v=>v.name) ).toEqual( [undefined] );
	} );

	//reviews-issue/251-resource-page.md #1:  the resources link reads schemaName, and with its column hidden the query left it
	//out - every row opened 'undefined/<slug>'
	describe( "with a rowLink's fields", ()=>{
		const settings:TableSettings = { columns: ["name", {name: "kind", hidden: true}], views: [{name: "Bare", columns: ["name"]}], rowLink: {fields: ["kind"], segments: r=>[`${r["kind"]}`, `${r.slug}`]} };
		const selects = ( view:View )=>/\{ (.*) \}$/.exec( view.query(0).text )![1].split( " " );
		it( 'queries them in every system view, shown or not', ()=>{
			const views = QLListResolver.systemViews( schema, settings );
			expect( views.map(v=>displayed(v).includes("kind")) ).toEqual( [false, false] );
			expect( views.map(v=>selects(v).includes("kind")) ).toEqual( [true, true] );
		} );
		it( "queries them in a saved view, which persists only what it shows", ()=>{
			const [route] = QLListResolver.systemViews( schema, settings );
			const saved = JSON.parse( JSON.stringify(new View(route).toJson(settings)) );
			expect( saved.fields.map( (f:{name:string})=>f.name ) ).not.toContain( "kind" );
			expect( selects(new View(saved, schema, route)) ).toContain( "kind" );
		} );
		it( 'leaves a hidden column out where no rowLink names it', ()=>{
			const [route] = QLListResolver.systemViews( schema, {...settings, rowLink: undefined} );
			expect( selects(route) ).not.toContain( "kind" );
		} );
	} );

	it( 'turns a declared filter into the query the settings panel would have built', ()=>{
		const [, kinds] = QLListResolver.systemViews( schema, { columns: ["name"], views: [
			{ name: "Kinds", filters: [{name: "kind", value: ["a", "<null>"]}] }
		] } );
		expect( kinds.fieldFilters ).toHaveLength( 1 );
		expect( kinds.fieldFilters[0].field.name ).toBe( "kind" );
		expect( kinds.fieldFilters[0].filter.operator ).toBe( Operator.In );
		const q = kinds.query( 0 );
		expect( q.text ).toContain( "kind:$kind" );
		expect( q.vars["kind"] ).toEqual( ["a", null] );//"<null>" leaves as a JSON null, as the panel's does
	} );

	//The default view can be the filtered one - resources opens on its table rows and keeps an unfiltered 'All' beside it.
	it( 'filters the default view when the route declares filters, and not the views beside it', ()=>{
		const [tables, all] = QLListResolver.systemViews( schema, { viewName: "Tables", columns: ["name"], filters: [{name: "kind", value: ["<null>"]}], views: [{name: "All"}] } );
		expect( tables.name ).toBe( "Tables" );
		expect( tables.query( 0 ).vars["kind"] ).toEqual( [null] );
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
		const data = { schema, profile, routing: new ListRoute("things"), columns: {}, pageSettings: {} } as unknown as QLListData;
		const y = await QLListResolver.loadOrFail( ql, data, null );
		expect( y.error ).toBe( refused );
		expect( y.results ).toEqual( {[schema.collectionName]: []} );
		expect( y.schema ).toBe( schema );
	} );
} );

//reviews/m3-closing.md #38:  a list name the route does not declare - a typed or bookmarked /access/nonsense - opened the
//first declared list (Users) under a "Nonsense" breadcrumb.  It is refused the way DetailResolver refuses '$new' where there is no Add.
@Component( {template: ''} ) class Dummy{}
describe( 'QLListResolver on a list name the route does not declare', ()=>{
	it( 'refuses it and goes back to the section, querying nothing', async ()=>{
		const error = vi.fn();
		const schemaWithEnums = vi.fn();
		TestBed.configureTestingModule( {providers: [
			provideRouter( [
				{ path: 'access', component: Dummy },
				{ path: 'access', children: [
					{ path: ':collectionDisplay', component: Dummy, providers: [QLListResolver], resolve: {data: QLListResolver}, data: {collections: ['users', 'resources']} }
				]}
			] ),
			{ provide: IGRAPHQL, useValue: {schemaWithEnums} },
			{ provide: SnackbarService, useValue: {error, exception: vi.fn()} },
			{ provide: RouteStore, useValue: {setChildren: vi.fn()} },
			{ provide: ProfileStore, useValue: {} }
		]} );
		const harness = await RouterTestingHarness.create();
		await harness.navigateByUrl( '/access/nonsense' );
		await new Promise( r=>setTimeout(r, 20) );
		expect( error ).toHaveBeenCalledWith( 'There is no nonsense list.' );
		expect( schemaWithEnums ).not.toHaveBeenCalled();
		expect( TestBed.inject(Router).url ).toBe( '/access' );
	} );
} );

//reviews-issue/231-paging.md #8:  QLList keeps the page it is on beside its sidenav children, and a list the router opens
//starts there - so a record's breadcrumb, or Back, returns to the page the record was opened from.
describe( 'QLListResolver remembers the page', ()=>{
	let stored:Map<string,string>;
	beforeEach( ()=>{
		stored = new Map();
		vi.stubGlobal( 'sessionStorage', { getItem: (k:string)=>stored.get(k) ?? null, setItem: (k:string, v:string)=>{ stored.set(k, v); }, removeItem: (k:string)=>{ stored.delete(k); } } );
	} );
	afterEach( ()=>vi.unstubAllGlobals() );
	const listData = ( childrenKey?:string )=>({ routing: new ListRoute("users"), childrenKey } as unknown as QLListData);

	it( 'under the key its children go under, and forgets the first page', ()=>{
		QLListResolver.saveSkip( listData(), 50 );
		QLListResolver.saveSkip( listData("apps/gateways/opchub"), 25 );
		expect( [...stored] ).toEqual( [["users/skip", "50"], ["apps/gateways/opchub/skip", "25"]] );
		expect( QLListResolver.savedSkip( listData() ) ).toBe( 50 );
		QLListResolver.saveSkip( listData(), 0 );
		expect( QLListResolver.savedSkip( listData() ) ).toBe( 0 );
		expect( stored.has("users/skip") ).toBe( false );
	} );

	it( 'reads nothing it cannot make sense of', ()=>{
		stored.set( "users/skip", "x" );
		expect( QLListResolver.savedSkip( listData() ) ).toBe( 0 );
		vi.stubGlobal( 'sessionStorage', undefined );//storage off
		expect( QLListResolver.savedSkip( listData() ) ).toBe( 0 );
		expect( ()=>QLListResolver.saveSkip( listData(), 25 ) ).not.toThrow();
	} );

	it( 'opens a list on the page it was left on', async ()=>{
		stored.set( "users/skip", "25" );
		const things = Array.from( {length: 40}, (_, i)=>({id: i+1, name: `thing ${i+1}`, slug: `thing${i+1}`}) );
		const query = vi.fn( async ( text:string )=>({things: things.slice( +(/skip:(\d+)/.exec(text)?.[1] ?? 0) )}) );
		TestBed.resetTestingModule();
		TestBed.configureTestingModule( {providers: [
			provideRouter( [ { path: 'access', children: [
				{ path: ':collectionDisplay', component: Dummy, providers: [QLListResolver], resolve: {data: QLListResolver}, data: {collections: [{path: 'users', data: {tableSettings: {columns: ["name"]}}}]} }
			]} ] ),
			{ provide: IGRAPHQL, useValue: {schemaWithEnums: async ()=>schema, query} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception: vi.fn()} },
			{ provide: RouteStore, useValue: {setChildren: vi.fn()} },
			{ provide: ProfileStore, useValue: {loadClassArray: async ()=>[]} }
		]} );
		const harness = await RouterTestingHarness.create();
		await harness.navigateByUrl( '/access/users' );
		expect( query.mock.calls[0][0] ).toContain( "skip:25" );
	} );
} );

//#231:  a list loads its view's Page Size and nothing after it, and the page has no count to page by - the server offers none.
//load() asks for one row past the page instead:  its presence is `more`, and the row itself never reaches the table.
describe( 'QLListResolver.load pages', ()=>{
	const rows = ( n:number, from=0 )=>Array.from( {length: n}, (_, i)=>({id: from+i+1, name: `thing ${from+i+1}`, slug: `thing${from+i+1}`}) );
	const data = ( limit:number, skip?:number )=>{
		const profile = new PageProfile();
		const view = new View( {columns: ["name"], sort: "name"}, schema );
		view.limit = limit;
		profile.views = [view];
		return { schema, profile, routing: new ListRoute("things"), columns: {}, pageSettings: {}, skip } as unknown as QLListData;
	};
	const ql = ( available:any[] )=>{
		const query = vi.fn( async ( text:string )=>{
			const limit = +( /limit:(\d+)/.exec(text)?.[1] ?? available.length );
			const skip = +( /skip:(\d+)/.exec(text)?.[1] ?? 0 );
			return { things: available.slice(skip, skip+limit) };
		} );
		return { ql: {query} as any, query };
	};

	it( 'asks for one row past the page, and hands the table only the page', async ()=>{
		const { ql: q, query } = ql( rows(40) );
		const y = await QLListResolver.load( q, data(25), null );
		expect( query.mock.calls[0][0] ).toContain( "limit:26" );
		expect( y.results.things ).toHaveLength( 25 );
		expect( y.more ).toBe( true );
		expect( y.skip ).toBe( 0 );
	} );

	it( 'reports no more on a short last page, and where the page is exactly full', async ()=>{
		const last = await QLListResolver.load( ql(rows(40)).ql, data(25, 25), null );
		expect( last.results.things.map( (r:any)=>r.id ) ).toEqual( rows(15, 25).map( r=>r.id ) );
		expect( last.more ).toBe( false );
		expect( last.skip ).toBe( 25 );
		const full = await QLListResolver.load( ql(rows(25)).ql, data(25), null );
		expect( full.results.things ).toHaveLength( 25 );
		expect( full.more ).toBe( false );//the probe row is what tells a full page from a full list
	} );

	it( 'sends the offset, and the sidenav children are the page shown', async ()=>{
		const { ql: q, query } = ql( rows(40) );
		const setChildren = vi.fn();
		const y = await QLListResolver.load( q, data(25, 25), {setChildren} as unknown as RouteStore );
		expect( query.mock.calls[0][0] ).toContain( "skip:25" );
		expect( setChildren ).toHaveBeenCalledWith( "things", rows(15, 25).map( r=>({title: r.name, path: r.slug}) ) );
		expect( y.results.things ).toHaveLength( 15 );
	} );

	//reviews-issue/231-paging.md #5:  a gateway's Connections tab has routing.path gateways/<instance>, the browse tree's key
	it( 'writes the children under childrenKey where the data has one, and keeps it for the next page', async ()=>{
		const { ql: q } = ql( rows(40) );
		const setChildren = vi.fn();
		const y = await QLListResolver.load( q, {...data(25, 25), childrenKey: "apps/gateways/opchub"}, {setChildren} as unknown as RouteStore );
		expect( setChildren ).toHaveBeenCalledWith( "apps/gateways/opchub", rows(15, 25).map( r=>({title: r.name, path: r.slug}) ) );
		expect( y.childrenKey ).toBe( "apps/gateways/opchub" );
	} );

	//#251:  a child's path is where its row opens - a resource's is schema/slug
	it( "writes each child's path from the route's rowLink", async ()=>{
		const { ql: q } = ql( rows(2) );
		const setChildren = vi.fn();
		const things = data( 25 );
		things.routing = new ListRoute( {path: "things", data: {tableSettings: {rowLink: {fields: [], segments: (r)=>["schema", `${r.slug}`]}}} as any} );
		await QLListResolver.load( q, things, {setChildren} as unknown as RouteStore );
		expect( setChildren ).toHaveBeenCalledWith( "things", rows(2).map( r=>({title: r.name, path: `schema/${r.slug}`}) ) );
	} );
	it( "leaves a row its rowLink gives no page out of the children", async ()=>{
		const { ql: q } = ql( rows(3) );
		const setChildren = vi.fn();
		const things = data( 25 );
		const second = rows(3)[1].slug;
		things.routing = new ListRoute( {path: "things", data: {tableSettings: {rowLink: {fields: [], segments: (r)=>r.slug==second ? undefined : [`${r.slug}`]}}} as any} );
		await QLListResolver.load( q, things, {setChildren} as unknown as RouteStore );
		expect( setChildren ).toHaveBeenCalledWith( "things", rows(3).filter( r=>r.slug!=second ).map( r=>({title: r.name, path: r.slug}) ) );
	} );

	it( 'falls back to the first page when the one asked for has emptied', async ()=>{
		const { ql: q, query } = ql( rows(10) );//rows deleted since the page was read - or since it was remembered (#8)
		const y = await QLListResolver.load( q, data(25, 25), null );
		expect( query.mock.calls.map( c=>/skip:(\d+)/.exec(c[0])?.[1] ?? "0" ) ).toEqual( ["25", "0"] );
		expect( y.skip ).toBe( 0 );
		expect( y.results.things ).toHaveLength( 10 );
	} );

	it( 'leaves an unlimited view alone', async ()=>{
		const { ql: q, query } = ql( rows(40) );
		const y = await QLListResolver.load( q, data(0), null );
		expect( query.mock.calls[0][0] ).not.toContain( "limit:" );
		expect( query.mock.calls[0][0] ).toContain( 'orderBy:[{name:"asc"}]' );
		expect( y.results.things ).toHaveLength( 40 );
		expect( y.more ).toBe( false );
	} );

	//reviews-issue/231-paging.md #3:  offset paging over a sort with ties - two rows with the same name either side of a page
	//boundary - may show a row on both pages or on neither, and MSSQL refuses offset/fetch with no order by.
	describe( 'orders every page totally', ()=>{
		const sent = async ( sort:Sort[] )=>{
			const { ql: q, query } = ql( rows(40) );
			const d = data( 25, 25 );
			d.profile.view.sort = sort;
			await QLListResolver.load( q, d, null );
			return { text: query.mock.calls[0][0] as string, view: d.profile.view };
		};

		it( "breaks ties on id after the view's own sort, on a copy", async ()=>{
			const { text, view } = await sent( [{active: "name", direction: "asc"}] );
			expect( text ).toContain( 'orderBy:[{name:"asc"},{id:"asc"}]' );
			expect( view.sort ).toEqual( [{active: "name", direction: "asc"}] );//the profile's view is what the settings panel edits and a Save persists
		} );

		it( 'orders by id where the view has no sort', async ()=>{
			expect( (await sent( [] )).text ).toContain( 'orderBy:[{id:"asc"}]' );
		} );

		it( 'leaves a sort that already has id', async ()=>{
			expect( (await sent( [{active: "id", direction: "desc"}] )).text ).toContain( 'orderBy:[{id:"desc"}]' );
		} );
	} );
} );