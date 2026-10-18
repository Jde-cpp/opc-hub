import { type MockInstance, vi } from 'vitest';
import { HighchartsLoader } from './highcharts-loader';

//Under jsdom nothing serves the add-on folder, so every import fails:  what the loader makes of it is the answer to the fetch of
//highstock.js it makes then, stubbed here.
describe( 'HighchartsLoader', ()=>{
	const answer = ( status:number, type:string )=>async ()=>({ status, ok: status>=200 && status<300, headers: {get: ( name:string )=>name=='content-type' ? type : null} });
	const answering = ( fetch:( url:string )=>Promise<unknown> )=>{ const f = vi.fn( fetch ); vi.stubGlobal( 'fetch', f ); return f; };
	let info:MockInstance, warn:MockInstance;
	beforeEach( ()=>{ info = vi.spyOn( console, 'info' ).mockImplementation( ()=>{} ); warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} ); } );
	afterEach( ()=>{ vi.unstubAllGlobals(); info.mockRestore(); warn.mockRestore(); } );
	//the hub's 404, and the page nginx's try_files answers in its place:  the site has no add-on, for the page's life
	it.each( [['a 404', answer( 404, 'text/plain' )], ['the page in its place', answer( 200, 'text/html' )]] )( 'resolves undefined for %s, trying once', async ( _, fetch )=>{
		const fetched = answering( fetch );
		const loader = new HighchartsLoader();
		const first = loader.load();
		expect( loader.load() ).toBe( first );
		expect( await first ).toBeUndefined();
		expect( fetched ).toHaveBeenCalledTimes( 1 );
		expect( fetched.mock.calls[0][0] ).toMatch( /\/addons\/highcharts\/esm\/highstock\.js$/ );
		expect( info ).toHaveBeenCalledTimes( 1 );
		expect( warn ).not.toHaveBeenCalled();
		expect( loader.state() ).toBe( 'absent' );
	} );
	//the hub restarting, a partial copy, a script that throws:  the add-on may well be there, so the next load tries again
	it.each( [['a 503', answer( 503, 'text/plain' )], ['the script there', answer( 200, 'text/javascript; charset=utf-8' )], ['no answer', async ()=>{ throw new TypeError( 'Failed to fetch' ); }]] )( 'rejects for %s, and tries again', async ( _, fetch )=>{
		const fetched = answering( fetch );
		const loader = new HighchartsLoader();
		const first = loader.load();
		await expect( first ).rejects.toBeDefined();
		expect( warn ).toHaveBeenCalledTimes( 1 );
		expect( info ).not.toHaveBeenCalled();
		expect( loader.state() ).toBe( 'failed' );
		const second = loader.load();
		expect( second ).not.toBe( first );
		await expect( second ).rejects.toBeDefined();
		expect( fetched ).toHaveBeenCalledTimes( 2 );
	} );
	it( 'imports the accessibility module once highstock has run', async ()=>{
		const asked:string[] = [];
		let ran!:( m:any )=>void;
		const highstock = new Promise( r=>ran = r );
		const loader = new (class extends HighchartsLoader{ protected override importFile( file:string ){ asked.push( file ); return file=='highstock.js' ? highstock : Promise.resolve( {} ); } })();
		const loaded = loader.load();
		await Promise.resolve();
		expect( asked ).toEqual( ['highstock.js'] );
		expect( loader.state() ).toBeUndefined();
		ran( {default: 'H'} );
		expect( await loaded ).toBe( 'H' );
		expect( asked ).toEqual( ['highstock.js', 'modules/accessibility.js'] );
		expect( loader.state() ).toBe( 'loaded' );
		expect( loader.highcharts ).toBe( 'H' );
	} );
} );