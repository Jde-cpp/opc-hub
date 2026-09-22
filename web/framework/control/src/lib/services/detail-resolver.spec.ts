if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter, Router } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { vi } from 'vitest';
import { RecentVisits } from 'jde-spa';
import { SnackbarService } from '../shared/snackbar/snackbar-service';
import { IGRAPHQL } from './graphql';
import { DetailResolver } from './detail-resolver';

@Component( {template: ''} ) class Dummy{}

//reviews/m3-closing.md #25:  Recently visited drops a page that fails to open on NavigationError - but DetailResolver catches
//the failure and redirects to the list, which the router reports as a NavigationCancel.  A deleted role's tile stayed, and
//said "Slug not found" on every click.  The resolver forgets the page itself - for a missing row only:  a failed query is
//transient, and the page is still there.
describe( 'DetailResolver on a row that is gone', ()=>{
	async function open( querySingle:()=>Promise<unknown> ){
		const forget = vi.fn();
		const ql = {
			toCollectionName: ( s:string )=>s,
			schemaWithEnums: async ()=>({collectionName: 'roles', type: 'Role', fields: []}),
			slugQuery: ()=>'role(slug:"temp")',
			subQueries: ()=>[],
			querySingle
		};
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'access', children: [
				{ path: 'roles/:slug', component: Dummy, providers: [DetailResolver], resolve: {pageData: DetailResolver} },
				{ path: ':collectionDisplay', component: Dummy, data: {collections: ['roles']} }
			]}] ),
			{ provide: IGRAPHQL, useValue: ql },
			{ provide: SnackbarService, useValue: {exception: vi.fn(), error: vi.fn()} },
			{ provide: RecentVisits, useValue: {forget} }
		]} );
		const harness = await RouterTestingHarness.create();
		await harness.navigateByUrl( '/access/roles/temp' );
		await new Promise( r=>setTimeout(r, 20) );//the redirect is a second navigation
		return { router: TestBed.inject(Router), forget };
	}

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
