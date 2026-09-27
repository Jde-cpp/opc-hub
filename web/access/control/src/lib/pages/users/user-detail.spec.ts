if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { of } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { Operator, QLListResolver, SnackbarService, TableSchema, View, ViewFieldSettings } from 'jde-framework';
import { AccessService } from '../../services/access-service';
import { resourceTableSettings, UserDetail, userTableSettings } from './user-detail';

//angular-review3 L2: only group-detail clamped the stored tab index on $new.  The other detail pages restored an index
//that names a tab the @if drops for a new record, and mat-tab-group hard-loops on a selectedIndex it cannot resolve.
const create = ( row:any, mutations?:string[] )=>{
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}}})} },
		{ provide: Router, useValue: {navigate: ()=>{}} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} },
		{ provide: AccessService, useValue: {mutate: async ( ql:string )=>{ mutations?.push( ql ); }} }
	]});
	const page = TestBed.createComponent( UserDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit since C2 - createComponent alone does not call it
	return page;
};

describe( 'UserDetail tab index', ()=>{
	beforeEach( ()=>localStorage.setItem('userDetail', '3') );//Effective rights - the last tab an existing user has
	afterEach( ()=>localStorage.removeItem('userDetail') );

	it( 'clamps to Properties for a new user', ()=>{
		expect( create({}).tabIndex() ).toBe( 0 );
	} );

	it( 'keeps the stored index for an existing user', ()=>{
		expect( create({id: 7, name: "bob"}).tabIndex() ).toBe( 3 );
	} );
} );

//The soft delete client-detail has had:  the generic delete<Type>/restore<Type> the ql schema advertises for every table.
describe( 'UserDetail delete', ()=>{
	it( 'is offered for a saved user', ()=>{
		expect( create({id: 7, name: "bob"}).isNew ).toBe( false );
	} );

	it( 'is disabled for a new user', ()=>{//TestBed takes one configure per test - the two rows cannot share an `it`
		expect( create({}).isNew ).toBe( true );
	} );

	it( 'deletes by id', async ()=>{
		const sent:string[] = [];
		const page = create( {id: 7, name: "bob"}, sent );
		expect( page.isDeleted ).toBe( false );
		await page.onDeleteClick();
		expect( sent ).toEqual( ["deleteUser(id:7)"] );
	} );

	it( 'restores a row the show-deleted view returned', async ()=>{
		const sent:string[] = [];
		const page = create( {id: 7, name: "bob", deleted: "2026-09-09T00:00:00Z"}, sent );
		expect( page.isDeleted ).toBe( true );
		await page.onDeleteClick();
		expect( sent ).toEqual( ["restoreUser(id:7)"] );
	} );
} );

//the server authenticates a logon by loginName+provider (AuthenticateAwait), so both are fixed once the row exists - and a
//key user's provider doubly so, since changing it swaps key-properties for the generic form mid-edit.  One list feeds both forms.
describe( 'UserDetail readonly fields', ()=>{
	it( 'fixes provider and login name', ()=>{
		expect( create({id: 7, name: "bob"}).readonlyFields ).toEqual( ["provider", "loginName"] );
	} );
} );

//A Google/password identity renders the generic properties form, which builds a field per schema column - so every
//certificate column key-properties owns has to be excluded there or it shows up as an unfillable input (Fingerprint did).
describe( 'UserDetail excluded columns', ()=>{
	it( 'drops every certificate column from the generic form', ()=>{
		const excluded = create( {id: 7, provider: "Google"} ).excludedColumns;
		for( const field of ["modulus", "exponent", "issuer", "subjectAlt", "distinguished", "expiration", "fingerprint"] )
			expect( excluded ).toContain( field );
	} );
} );

//review3 L6: 'resources' is the one collection under access's ':collectionDisplay' with no sibling ':slug' detail
//route, so a row click there dead-ended in a NavigationError.  QLList honours canNavigate; this is the wiring.
describe( 'resourceTableSettings', ()=>{
	const column = ( name:string )=>resourceTableSettings.columns!.find( c=>typeof c!="string" && c.name==name ) as ViewFieldSettings;

	it( 'does not offer a row click-through', ()=>{
		expect( resourceTableSettings.canNavigate ).toBe( false );
	} );

	it( 'orders by schema then name, and shows the schema', ()=>{
		expect( column("schemaName")?.displayName ).toBe( "Schema" );
		expect( resourceTableSettings.sort ).toEqual( [{active:"schemaName", direction:"asc"}, {active:"name", direction:"asc"}] );
	} );

	//`deleted` on a resource is the authorizer's switch, not a trash can:  Authorize::Test skips a deleted resource ("not
	//enabled"), so the row the switch is OFF for is the one nothing checks permissions on.  It has to read as Enforced, or
	//an operator turning enforcement on thinks they are restoring something from the bin.
	it( 'renders deleted as the Enforced switch, with a verb for each direction', ()=>{
		const enforced = column( "deleted" );
		expect( enforced?.displayName ).toBe( "Enforced" );
		expect( enforced?.liveToggle?.enable ).toBe( "Enforce" );
		expect( enforced?.liveToggle?.disable ).toBe( "Stop enforcing" );
		expect( enforced?.liveToggle?.enableMessage ).toMatch( /lose your own access/ );
	} );

	//Neither is grantable on this table - access-meta gives `resources` ops Delete and Subscribe only - so offering either
	//would be offering a button whose mutation the server must refuse.
	it( 'offers neither Add nor Purge', ()=>{
		expect( resourceTableSettings.canAdd ).toBe( false );
		expect( resourceTableSettings.canPurge ).toBe( false );
	} );

	//reviews/m3-closing.md #32:  a user is only created by signing in, so the list has no Add and its empty state points at none
	it( 'offers no Add on users', ()=>{
		expect( userTableSettings.canAdd ).toBe( false );
		expect( userTableSettings.empty?.add ).toBeUndefined();
	} );

	//A node-scoped resource (criteria set) is minted when a role is granted on a node and shares its table's slug, so on a
	//page with no criteria column it reads as a duplicate row.  The page lists the table rows only - one view, no way to the node rows.
	it( 'lists only the criteria-less rows, as a single Tables view', ()=>{
		const scalar = ( name:string, type:string="String" )=>({ name, type: { kind: "SCALAR", name: type } });
		const schema = new TableSchema( { name: "Resource", fields: [
			{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
			{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
			scalar("schemaName"), scalar("name"), scalar("criteria"), scalar("deleted", "DateTime"), scalar("description")
		] } );
		const views = QLListResolver.systemViews( schema, resourceTableSettings );
		expect( views.map(v=>v.name) ).toEqual( ["Tables"] );
		const q = views[0].query( false, 0 );
		expect( q.text ).toContain( "criteria:$criteria" );
		expect( q.vars["criteria"] ).toEqual( [null] );
	} );
} );

//The users list's three system views.  'Users' and 'Certs' began life as one user's saved views; they are declared on
//the route now so every user gets them, and the default view is labelled 'All' instead of the framework's 'default'.
describe( 'userTableSettings views', ()=>{
	const scalar = ( name:string, type:string="String" )=>({ name, type: { kind: "SCALAR", name: type } });
	const schema = new TableSchema( { name: "User", fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "provider", type: { kind: "OBJECT", name: "Provider" } },
		scalar("email"), scalar("loginName"), scalar("modulus"), scalar("issuer"), scalar("distinguished"), scalar("subjectAlt"),
		scalar("expiration", "DateTime"), scalar("deleted", "DateTime"), scalar("description"), scalar("isGroup", "Boolean")
	] } );
	const displayed = ( v:View )=>v.fields.filter( f=>f.displayed ).map( f=>f.name );
	const views = QLListResolver.systemViews( schema, userTableSettings );
	const byName = ( name:string )=>views.find( v=>v.name==name )!;

	it( 'are All, Users and Certs - system views, in that order', ()=>{
		expect( views.map(v=>v.name) ).toEqual( ["All", "Users", "Certs"] );
		expect( views.every(v=>v.isSystem) ).toBe( true );
	} );

	it( 'Users lists the non-certificate identities with their login columns', ()=>{
		expect( displayed(byName("Users")) ).toEqual( ["name", "provider", "email", "loginName", "description"] );
		const q = byName( "Users" ).query( false, 0 );
		expect( q.text ).toContain( "issuer:$issuer" );
		expect( q.vars["issuer"] ).toEqual( [null] );
	} );

	it( 'Certs lists the key identities with their certificate columns', ()=>{
		expect( displayed(byName("Certs")) ).toEqual( ["slug", "modulus", "issuer", "distinguished", "subjectAlt", "expiration", "description"] );
		expect( byName("Certs").fieldFilters.map(f=>[f.field.name, f.filter.operator, f.filter.value]) ).toEqual( [["provider", Operator.In, ["Key"]]] );
		expect( byName("Certs").sort ).toEqual( [{active: "slug", direction: "asc"}] );//name is not a displayed column there
	} );
} );
