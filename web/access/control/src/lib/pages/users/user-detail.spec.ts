import { TestBed } from '@angular/core/testing';
import { Operator, QLListResolver, View } from 'jde-framework';
import { AccessService } from '../../services/access-service';
import { UserDetail } from './user-detail';
import { userTableSettings } from './user-table-settings';
import { configureDetailPage, object, scalar, slugSchema } from 'jde-framework/testing';

//angular-review3 L2: only group-detail clamped the stored tab index on $new.  The other detail pages restored an index
//that names a tab the @if drops for a new record, and mat-tab-group hard-loops on a selectedIndex it cannot resolve.
const create = ( row:any )=>{
	const mutate = vi.fn( async ( _ql:string )=>{} );
	configureDetailPage( row, {providers: [
		{ provide: AccessService, useValue: {mutate} }
	]} );
	const page = TestBed.createComponent( UserDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit since C2 - createComponent alone does not call it
	return { page, mutate };
};

describe( 'UserDetail tab index', ()=>{
	beforeEach( ()=>localStorage.setItem('userDetail/tabIndex', '3') );//Effective rights - the last tab an existing user has
	afterEach( ()=>localStorage.removeItem('userDetail/tabIndex') );

	it( 'clamps to Properties for a new user', ()=>{
		expect( create({}).page.tabIndex() ).toBe( 0 );
	} );

	it( 'keeps the stored index for an existing user', ()=>{
		expect( create({id: 7, name: "bob"}).page.tabIndex() ).toBe( 3 );
	} );
} );

//The soft delete client-detail has had:  the generic delete<Type>/restore<Type> the ql schema advertises for every table.
describe( 'UserDetail delete', ()=>{
	it( 'is offered for a saved user', ()=>{
		expect( create({id: 7, name: "bob"}).page.isNew ).toBe( false );
	} );

	it( 'is disabled for a new user', ()=>{//TestBed takes one configure per test - the two rows cannot share an `it`
		expect( create({}).page.isNew ).toBe( true );
	} );

	it( 'deletes by id', async ()=>{
		const { page, mutate } = create( {id: 7, name: "bob"} );
		expect( page.isDeleted ).toBe( false );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "deleteUser(id:7)" );
	} );

	it( 'restores a row the show-deleted view returned', async ()=>{
		const { page, mutate } = create( {id: 7, name: "bob", deleted: "2026-09-09T00:00:00Z"} );
		expect( page.isDeleted ).toBe( true );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "restoreUser(id:7)" );
	} );
} );

//the server authenticates a logon by loginName+provider (AuthenticateAwait), so both are fixed once the row exists - and a
//key user's provider doubly so, since changing it swaps key-properties for the generic form mid-edit.  One list feeds both forms.
describe( 'UserDetail readonly fields', ()=>{
	it( 'fixes provider and login name', ()=>{
		expect( create({id: 7, name: "bob"}).page.readonlyFields ).toEqual( ["provider", "loginName"] );
	} );
} );

//A Google/password identity renders the generic properties form, which builds a field per schema column - so every
//certificate column key-properties owns has to be excluded there or it shows up as an unfillable input (Fingerprint did).
describe( 'UserDetail excluded columns', ()=>{
	it( 'drops every certificate column from the generic form', ()=>{
		const excluded = create( {id: 7, provider: "Google"} ).page.excludedColumns;
		for( const field of ["modulus", "exponent", "issuer", "subjectAlt", "distinguished", "expiration", "fingerprint"] )
			expect( excluded ).toContain( field );
	} );
} );

describe( 'userTableSettings', ()=>{
	//reviews/m3-closing.md #32:  a user is only created by signing in, so the list has no Add and its empty state points at none
	it( 'offers no Add on users', ()=>{
		expect( userTableSettings.canAdd ).toBe( false );
		expect( userTableSettings.empty?.add ).toBeUndefined();
	} );
} );

//The users list's three system views.  'Users' and 'Certs' began life as one user's saved views; they are declared on
//the route now so every user gets them, and the default view is labelled 'All' instead of the framework's 'Default'.
describe( 'userTableSettings views', ()=>{
	const schema = slugSchema( "User", object("provider", "Provider"),
		scalar("email"), scalar("loginName"), scalar("modulus"), scalar("issuer"), scalar("distinguished"), scalar("subjectAlt"),
		scalar("expiration", "DateTime"), scalar("deleted", "DateTime"), scalar("description"), scalar("isGroup", "Boolean") );
	const displayed = ( v:View )=>v.fields.filter( f=>f.displayed ).map( f=>f.name );
	const views = QLListResolver.systemViews( schema, userTableSettings );
	const byName = ( name:string )=>views.find( v=>v.name==name )!;

	it( 'are All, Users and Certs - system views, in that order - then the framework\'s Deleted', ()=>{
		expect( views.map(v=>v.name) ).toEqual( ["All", "Users", "Certs", "Deleted"] );
		expect( views.every(v=>v.isSystem) ).toBe( true );
	} );

	it( 'Users lists the non-certificate identities with their login columns', ()=>{
		expect( displayed(byName("Users")) ).toEqual( ["name", "provider", "email", "loginName", "description"] );
		const q = byName( "Users" ).query( 0 );
		expect( q.text ).toContain( "issuer:$issuer" );
		expect( q.vars["issuer"] ).toEqual( [null] );
	} );

	it( 'Certs lists the key identities with their certificate columns', ()=>{
		expect( displayed(byName("Certs")) ).toEqual( ["slug", "modulus", "issuer", "distinguished", "subjectAlt", "expiration", "description"] );
		expect( byName("Certs").fieldFilters.map(f=>[f.field.name, f.filter.operator, f.filter.value]) ).toEqual( [["provider", Operator.In, ["Key"]]] );
		expect( byName("Certs").sort ).toEqual( [{active: "slug", direction: "asc"}] );//name is not a displayed column there
	} );
} );