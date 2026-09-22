//the vitest environment provides window/document but no localStorage - back the bare-global references with an
//in-memory one BEFORE importing jde-spa (see google-relogin.spec.ts).
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
import { Router } from '@angular/router';
import { GATEWAY_SERVICE, NodeSearchProvider, NodeSearchRow } from 'jde-opc';

type Call = { ql:string; vars:any };
function gateway( slug:string, rows:NodeSearchRow[]|Error ){
	const calls:Call[] = [];
	return { slug, calls, queryArray: async ( ql:string, vars:any )=>{ calls.push( {ql, vars} ); if( rows instanceof Error ) throw rows; return rows; } };
}
const lamp:NodeSearchRow = { connection: {slug: 'cn1', name: 'Line 1'}, path: '4~Examples/4~Lamp 1', name: 'Lamp 1', nodeClass: 1, depth: 2 };
const rpm:NodeSearchRow = { connection: {slug: 'cn2', name: 'Line 2'}, path: 'pump1/motorRpm', name: 'motorRpm', nodeClass: 2, depth: 2 };

describe('NodeSearchProvider', () => {
	function setup( url:string, gateways:ReturnType<typeof gateway>[] ){
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: {url} },
			{ provide: GATEWAY_SERVICE, useValue: { gateway: async (t:string)=>gateways.find(g=>g.slug==t), gateways: async ()=>gateways } },
		]});
		return TestBed.inject( NodeSearchProvider );
	}

	it('inside a connection searches only that connection, and builds an encodable commands array', async () => {
		const gw = gateway( 'gw', [lamp] ), other = gateway( 'gw2', [rpm] );
		const results = await setup( '/gateways/gw/cn1/4~Examples?x=1', [gw, other] ).search( 'lamp', undefined, 5 );
		expect( gw.calls ).toHaveLength( 1 );
		expect( gw.calls[0].vars ).toEqual( {opc: 'cn1', text: 'lamp', limit: 5} );
		expect( gw.calls[0].ql ).toContain( 'opc:$opc' );
		expect( other.calls ).toHaveLength( 0 );
		expect( results ).toHaveLength( 1 );
		expect( results[0] ).toMatchObject( { title: 'Lamp 1', summary: 'Line 1/Examples/Lamp 1', icon: 'folder', rank: 0, route: ['/gateways', 'gw', 'cn1', '4~Examples', '4~Lamp 1'] } );//the summary names segments as the breadcrumbs do (3703ff0e);  this copy of the spec had not followed
	});

	it('elsewhere fans out over every gateway without an opc, ignoring one that fails', async () => {
		const warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		const gw = gateway( 'gw', [rpm] ), dead = gateway( 'gw2', new Error('unreachable') );
		const results = await setup( '/access/users', [gw, dead] ).search( 'rpm', undefined, 5 );
		expect( gw.calls[0].vars ).toEqual( {text: 'rpm', limit: 5} );
		expect( gw.calls[0].ql ).not.toContain( 'opc:' );
		expect( results.map(r=>r.icon) ).toEqual( ['label'] );
		expect( results[0].rank ).toBe( 1 );//'motorRpm' contains but does not start with 'rpm'
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	});

	//reviews/m3-closing.md #9:  a Variable (or a Method) has no page - it is a row on its parent's - so its hit opens the parent;
	//the summary still names the node itself.  One directly under Objects opens the connection.
	it('opens a variable\'s parent, naming the variable in the summary', async () => {
		const top:NodeSearchRow = { connection: {slug: 'cn2', name: 'Line 2'}, path: 'status', name: 'status', nodeClass: 2, depth: 1 };
		const results = await setup( '/access/users', [gateway( 'gw', [rpm, top] )] ).search( 'r', undefined, 5 );
		expect( results[0] ).toMatchObject( { title: 'motorRpm', summary: 'Line 2/pump1/motorRpm', route: ['/gateways', 'gw', 'cn2', 'pump1'] } );
		expect( results[1].route ).toEqual( ['/gateways', 'gw', 'cn2'] );
	});

	it('returns nothing for blank text without asking any gateway', async () => {
		const gw = gateway( 'gw', [rpm] );
		expect( await setup( '/', [gw] ).search( '', 'node', 5 ) ).toEqual( [] );
		expect( gw.calls ).toHaveLength( 0 );
	});
});
