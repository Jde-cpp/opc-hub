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
import { ProfileStore, RecentVisits } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { ENodeClass, OpcObject, UaNode, Variable } from '../../model/node';
import { GATEWAY_SERVICE } from '../gateway-service';
import { OPC_STORE, OpcStore } from '../opc-store';
import { NodeResolver } from './node-resolver';

@Component( {template: ''} ) class Dummy{}

const pump1 = new OpcObject( {ns: 2, i: 10, name: "pump1", browse: {ns: 2, name: "pump1"}} as any );
const serverDescription = { connection: {id: 1, slug: 'cn', name: 'Line 1', url: 'opc.tcp://plc:4840', certificateUri: '', defaultBrowseNs: 1}, desc: {}, policy: '', mode: 'None', namespaces: [] };
const nodes:Record<string, any> = {//the gateway's answer to node( path ){ … nodeClass … } per browse path
	'2~pump1': { ns: 2, i: 10, name: 'pump1', nodeClass: ENodeClass.Object, parents: [] },
	'2~pump1/2~flow': { ns: 2, i: 11, name: 'flow', nodeClass: ENodeClass.Variable, parents: [{ns: 2, i: 10, name: 'pump1', path: '2~pump1'}] }
};
//gatewayDown:  the gateway does not answer at all
async function open( url:string, browsed:boolean, gatewayDown = false ){
	const exception = vi.fn();
	const forget = vi.fn();
	const browseObjectsFolder = vi.fn( async ( _cnnctn:string, parent:UaNode )=>{
		if( parent.isVariable )
			throw new EvalError( "Cannot browse children of variable node." );
		return [];
	} );
	const gateway = { slug: 'gw', browseObjectsFolder, errorCodeText: async ()=>"", query: async ( ql:string, vars:any )=>{
		if( ql.includes("serverDescription") )
			return serverDescription;
		return { node: nodes[vars.path] ?? {sc: 0x806f0000} };
	} };
	TestBed.configureTestingModule( {providers: [
		provideRouter( [{ path: 'gateways/:gateway/:connection', children: [
			{ path: '**', component: Dummy, providers: [NodeResolver], resolve: {pageData: NodeResolver}, runGuardsAndResolvers: "pathParamsOrQueryParamsChange" }
		]}] ),
		{ provide: GATEWAY_SERVICE, useValue: {gateway: async ()=>{ if( gatewayDown ) throw new Error( "Failed to fetch" ); return gateway; }} },
		{ provide: OPC_STORE, useExisting: OpcStore },
		{ provide: SnackbarService, useValue: {exception, error: vi.fn()} },
		{ provide: ProfileStore, useValue: {} },
		{ provide: RecentVisits, useValue: {forget} }
	]} );
	const store = TestBed.inject( OpcStore );
	store.nodeView.set( <any>{sortNodes: ( n:UaNode[] )=>n} );
	if( browsed ){//the parent's page was open:  browseObjectsFolder stored its children, the Variable among them
		store.setNodes( 'gw', 'cn', OpcObject.rootNode, [pump1] );
		store.setNodes( 'gw', 'cn', pump1, [new Variable({ns: 2, i: 11, name: "flow", browse: {ns: 2, name: "flow"}} as any, pump1)] );
	}
	const harness = await RouterTestingHarness.create();
	await harness.navigateByUrl( url );
	await new Promise( r=>setTimeout(r, 20) );//the redirect is a second navigation
	return { router: TestBed.inject(Router), exception, browseObjectsFolder, forget };
}

//reviews/m3-closing.md #9:  a Variable has no page of its own - its value, status and subscribe box are a row on its parent's -
//yet a search hit, or a bookmark, could route to one.  With the parent browsed the stored Variable was handed to
//browseObjectsFolder, which threw, and the page said "Not found." about a node that exists;  without, the path query's answer
//was wrapped as an object and its own children listed under the variable's name.  Either way the page to show is the parent's.
describe( 'NodeResolver on a Variable', ()=>{
	it( 'shows the parent, not a false "Not found.", when the parent was browsed', async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/2~pump1/2~flow', true );
		expect( exception ).not.toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/2~pump1' );
	} );

	it( "shows the parent, not the variable's own children, when it was not", async ()=>{
		const { router, exception, browseObjectsFolder } = await open( '/gateways/gw/cn/2~pump1/2~flow', false );
		expect( exception ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/2~pump1' );
		expect( browseObjectsFolder.mock.calls.map(c=>(c[1] as UaNode).name) ).not.toContain( "flow" );
	} );
} );

//reviews/m3-closing.md #25:  a removed or renamed node's Recently visited tile stayed - the resolver redirects on failure, a
//NavigationCancel, not the NavigationError RecentVisits drops a page on.  It forgets the page itself when the server says the
//path is not there - not when the gateway or the OPC server is merely down, or an outage would empty every node tile.
describe( 'NodeResolver on a node that is gone', ()=>{
	it( "forgets the page of a path the server does not have, and still goes up a level", async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/2~pump1/2~gone', false );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).toHaveBeenCalledWith( '/gateways/gw/cn/2~pump1/2~gone' );
		expect( router.url ).toBe( '/gateways/gw/cn/2~pump1' );
	} );

	it( 'keeps the page while the gateway is down', async ()=>{
		const { exception, forget } = await open( '/gateways/gw/cn/2~pump1', false, true );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
	} );
} );
