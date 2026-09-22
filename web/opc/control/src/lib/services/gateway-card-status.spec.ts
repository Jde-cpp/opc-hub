//the vitest environment provides window/document but no localStorage - back the bare-global references with an
//in-memory one BEFORE importing jde-spa/jde-framework (see the site specs).
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
import { HttpErrorResponse } from '@angular/common/http';
import { GatewayCardStatus } from './gateway-card-status';
import { GATEWAY_SERVICE } from './gateway-service';

//a gateway whose serverConnections query answers `rows`, or rejects with `error`.
const gateway = ( slug:string, answer:{rows?:number, error?:unknown} )=>( {
	slug,
	query: ()=>answer.error!==undefined
		? Promise.reject( answer.error )
		: Promise.resolve( {serverConnections: Array.from({length: answer.rows ?? 0}, (_, i)=>({slug: `c${i}`}))} )
} );
const refused = ( status:number )=>new HttpErrorResponse( {status, statusText: status==403 ? 'Forbidden' : 'Unauthorized'} );
const down = new HttpErrorResponse( {status: 0, statusText: 'Unknown Error'} );

describe( 'GatewayCardStatus', ()=>{
	const status = ( gateways:unknown[] )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule( {providers: [{provide: GATEWAY_SERVICE, useValue: {gateways: ()=>Promise.resolve(gateways)}}]} );
		return TestBed.inject( GatewayCardStatus ).status();
	};

	//the CARD_STATUS contract:  a status that rejects leaves the tile its static summary and the header without a line.
	it( 'rejects when every gateway refuses the rows (403), so the tile keeps its static summary', async ()=>{
		await expect( status([gateway('gw1', {error: refused(403)})]) ).rejects.toBeInstanceOf( HttpErrorResponse );
	});

	it( 'treats a 401 as a refusal too', async ()=>{
		await expect( status([gateway('gw1', {error: refused(401)}), gateway('gw2', {error: refused(403)})]) ).rejects.toBeInstanceOf( HttpErrorResponse );
	});

	it( 'counts only the gateways that did not answer, and leaves a refusal out of the figure', async ()=>{
		const value = await status( [gateway('gw1', {rows: 2}), gateway('gw2', {error: refused(403)}), gateway('gw3', {error: down})] );
		expect( value.figure ).toBe( 2 );
		expect( value.detail ).toBe( '1 gateway not answering' );
		expect( value.summary ).toBe( '2 OPC connections on 2 gateways · 1 gateway not answering' );
		expect( value.warn ).toBe( true );
	});

	it( 'a refusal beside an answering gateway is no warning', async ()=>{
		const value = await status( [gateway('gw1', {rows: 3}), gateway('gw2', {error: refused(403)})] );
		expect( value.detail ).toBe( 'on 1 gateway' );
		expect( value.summary ).toBe( '3 OPC connections on 1 gateway' );
		expect( value.warn ).toBe( false );
	});

	it( 'still flags a gateway that is down', async ()=>{
		const value = await status( [gateway('gw1', {error: down})] );
		expect( value.figure ).toBe( 0 );
		expect( value.detail ).toBe( '1 gateway not answering' );
		expect( value.warn ).toBe( true );
	});

	it( 'still says so when no gateway is registered', async ()=>{
		const value = await status( [] );
		expect( value.summary ).toBe( 'No gateway registered' );
		expect( value.warn ).toBe( true );
	});
});
