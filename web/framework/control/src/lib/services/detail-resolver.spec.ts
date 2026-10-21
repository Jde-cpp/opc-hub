import { IGRAPHQL } from './graphql';
import { DetailResolver } from './detail-resolver';
import { Blank } from 'jde-spa/testing';
import { openRoute, slugQl } from 'jde-framework/testing';
import { TestBed } from '@angular/core/testing';
import { RouteItem, RouteStore } from 'jde-spa';

//reviews/m3-closing.md #25:  Recently visited drops a page that fails to open on NavigationError - but DetailResolver catches
//the failure and redirects to the list, which the router reports as a NavigationCancel.  A deleted role's tile stayed, and
//said "Slug not found" on every click.  The resolver forgets the page itself - for a missing row only:  a failed query is
//transient, and the page is still there.
describe( 'DetailResolver on a row that is gone', ()=>{
	const open = ( querySingle:()=>Promise<unknown> )=>openRoute( {routes: [{ path: 'access', children: [
		{ path: 'roles/:slug', component: Blank, providers: [DetailResolver], resolve: {pageData: DetailResolver} },
		{ path: ':collectionDisplay', component: Blank, data: {collections: ['roles']} }
	]}], providers: [{ provide: IGRAPHQL, useValue: slugQl( querySingle ) }], url: '/access/roles/temp'} );

	it( "forgets a deleted row's page, and still goes back to the list", async ()=>{
		const { router, forget } = await open( async ()=>null );
		expect( forget ).toHaveBeenCalledWith( '/access/roles/temp' );
		expect( router.url ).toBe( '/access/roles' );
	} );

	it( 'keeps the page when the query merely failed', async ()=>{
		const { router, forget } = await open( async ()=>{ throw new Error( "(500)server error" ); } );
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/access/roles' );
	} );
} );

//reviews/m3-closing.md #32:  /access/users has no Add, but a typed or bookmarked 'users/$new' still opened a blank form.
describe( 'DetailResolver on $new for a list with no Add', ()=>{
	it( 'refuses it and goes back to the list', async ()=>{
		const schemaWithEnums = vi.fn( async ()=>({collectionName: 'users', type: 'User', fields: []}) );
		const { router, error } = await openRoute( {routes: [{ path: 'access', children: [
			{ path: 'users/:slug', component: Blank, providers: [DetailResolver], resolve: {pageData: DetailResolver} },
			{ path: ':collectionDisplay', component: Blank, data: {collections: [{path: 'users', data: {tableSettings: {canAdd: false}}}]} }
		]}], providers: [{ provide: IGRAPHQL, useValue: {toCollectionName: ( s:string )=>s, schemaWithEnums} }], url: '/access/users/$new'} );
		expect( error ).toHaveBeenCalled();
		expect( schemaWithEnums ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/access/users' );
	} );
} );

//reviews/angular-refactor.md B12:  the list keeps its rows under its absolute url, and the record's page reads them from there -
//it read the bare collection name, a key spelled for this reader alone
describe( 'DetailResolver siblings', ()=>{
	afterEach( ()=>localStorage.clear() );

	it( "reads the list's rows from under the list's url", async ()=>{
		const { leaf } = await openRoute( {routes: [{ path: 'access', children: [
			{ path: 'roles/:slug', component: Blank, providers: [DetailResolver], resolve: {pageData: DetailResolver} },
			{ path: ':collectionDisplay', component: Blank, data: {collections: ['roles']} }
		]}], providers: [{ provide: IGRAPHQL, useValue: slugQl( async ()=>({id: 1, name: 'Temp', slug: 'temp'}) ) }], url: '/access/roles/temp',
			before: ()=>TestBed.inject( RouteStore ).setChildren( 'access/roles', [{path: 'temp', title: 'Temporary'}, {path: 'ops', title: 'Operators'}] )} );
		const routing = leaf().data['pageData'].routing;
		expect( routing.siblings.map( (s:RouteItem)=>s.path ) ).toEqual( ['temp', 'ops'] );
		expect( routing.title ).toBe( 'Temporary' );
		expect( routing.parent.path ).toBe( '/access/roles' );
	} );
} );