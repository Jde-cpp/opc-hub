import { Location } from '@angular/common';
import { TestBed } from '@angular/core/testing';
import { provideRouter, Router, withNavigationErrorHandler } from '@angular/router';
import { chunkReloadKey, isChunkLoadError, reloadOnChunkLoadError } from './chunk-load-error';
import { Blank } from 'jde-spa/testing';

const chromeError = ()=>new TypeError( 'Failed to fetch dynamically imported module: http://localhost/chunk-AB12cd34.js' );

describe( 'isChunkLoadError', ()=>{
	it( 'knows each engine\'s failed dynamic import, and nothing else', ()=>{
		expect( isChunkLoadError(new TypeError('Failed to fetch dynamically imported module: https://hub/chunk-AB12cd34.js')) ).toBe( true );//Chrome
		expect( isChunkLoadError(new TypeError('error loading dynamically imported module: https://hub/chunk-AB12cd34.js')) ).toBe( true );//Firefox
		expect( isChunkLoadError(new TypeError('Importing a module script failed.')) ).toBe( true );//Safari
		expect( isChunkLoadError(new Error('Failed to fetch dynamically imported module')) ).toBe( false );
		expect( isChunkLoadError(new TypeError("Cannot read properties of undefined (reading 'id')")) ).toBe( false );
		expect( isChunkLoadError(undefined) ).toBe( false );
	});
});

//reviews-issue/lazy-routes.md #2
describe( 'reloadOnChunkLoadError', ()=>{
	const create = ()=>{
		const reload = vi.fn<( url:string )=>void>();
		TestBed.configureTestingModule( {providers: [
			provideRouter( [
				{ path: 'unloaded', loadComponent: ()=>Promise.reject(chromeError()) },
				{ path: 'other', loadComponent: ()=>Promise.reject(chromeError()) },
				{ path: 'broken', component: Blank, resolve: {x: ()=>{ throw new Error('deleted'); }} }
			], withNavigationErrorHandler(reloadOnChunkLoadError(reload)) )
		]} );
		return {
			reload,
			open: ( url:string )=>TestBed.inject( Router ).navigateByUrl( url ).catch( ()=>{} ),
			external: ( url:string )=>TestBed.inject( Location ).prepareExternalUrl( url )
		};
	};
	beforeEach( ()=>sessionStorage.removeItem( chunkReloadKey ) );
	afterEach( ()=>{
		vi.useRealTimers();
		vi.restoreAllMocks();
		sessionStorage.removeItem( chunkReloadKey );
	});

	it( 'reloads the page whose chunk failed, by its url under the base href', async ()=>{
		const { reload, open, external } = create();
		await open( '/unloaded?x=1' );
		expect( reload ).toHaveBeenCalledExactlyOnceWith( external('/unloaded?x=1') );
	});

	it( 'leaves every other navigation error alone', async ()=>{
		const { reload, open } = create();
		await open( '/broken' );
		expect( reload ).not.toHaveBeenCalled();
	});

	it( 'lets the same page fail again within a minute without reloading, but not another page, nor the same one after the minute', async ()=>{
		const { reload, open, external } = create();
		vi.useFakeTimers( {toFake: ['Date']} );
		await open( '/unloaded' );
		await open( '/unloaded' );
		expect( reload.mock.calls ).toEqual( [[external('/unloaded')]] );
		await open( '/other' );
		expect( reload ).toHaveBeenLastCalledWith( external('/other') );
		await open( '/other' );
		vi.setSystemTime( Date.now()+60_000 );
		await open( '/other' );
		expect( reload ).toHaveBeenCalledTimes( 3 );
	});

	it( 'does not reload when sessionStorage cannot record it', async ()=>{
		const { reload, open } = create();
		vi.spyOn( Storage.prototype, 'setItem' ).mockImplementation( ()=>{ throw new DOMException('denied', 'SecurityError'); } );
		await open( '/unloaded' );
		expect( reload ).not.toHaveBeenCalled();
	});
});