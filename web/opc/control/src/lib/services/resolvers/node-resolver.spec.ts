import { TestBed } from '@angular/core/testing';
import { ProfileStore } from 'jde-spa';
import { ENodeClass, OpcObject, UaNode, Variable } from '../../model/node';
import { GATEWAY_SERVICE } from '../gateway-service';
import { OPC_STORE, OpcStore } from '../opc-store';
import { NodeResolver } from './node-resolver';
import { Blank, profileStoreStub } from 'jde-spa/testing';
import { openRoute } from 'jde-framework/testing';
import { fakeGateway } from 'jde-opc/testing';

const pump1 = new OpcObject( {ns: 2, i: 10, name: "pump1", browse: {ns: 2, name: "pump1"}} as any );
const serverDescription = { connection: {id: 1, slug: 'cn', name: 'Line 1', url: 'opc.tcp://plc:4840', certificateUri: '', defaultBrowseNs: 1}, desc: {}, policy: '', mode: 'None', namespaces: [] };
const nodes:Record<string, any> = {//the gateway's answer to node( path ){ … nodeClass … } per browse path
	'2~pump1': { ns: 2, i: 10, name: 'pump1', nodeClass: ENodeClass.Object, parents: [] },
	'2~pump1/2~flow': { ns: 2, i: 11, name: 'flow', nodeClass: ENodeClass.Variable, parents: [{ns: 2, i: 10, name: 'pump1', path: '2~pump1'}] }
};
const flow = ()=>new Variable( {ns: 2, i: 11, name: "flow", browse: {ns: 2, name: "flow"}} as any, pump1 );
//gatewayDown:  the gateway does not answer at all.  groups:  the gateway's answer to historyGroups, or an error it throws for it.
async function open( url:string, browsed:boolean, gatewayDown = false, groups:any = undefined ){
	const browseObjectsFolder = vi.fn( async ( _cnnctn:string, parent:UaNode )=>{
		if( parent.isVariable )
			throw new EvalError( "Cannot browse children of variable node." );
		return groups ? [flow()] : [];
	} );
	const postQL = vi.fn( async ( ql:string, _vars:any )=>{
		if( !ql.startsWith("historyGroups") )
			throw new Error( `unexpected post: ${ql}` );
		if( groups instanceof Error )
			throw groups;
		return { historyGroups: groups };
	} );
	const gateway = Object.assign( fakeGateway( 'gw' ), { browseObjectsFolder, postQL, errorCodeText: async ()=>"", query: async ( ql:string, vars:any )=>{
		if( ql.includes("serverDescription") )
			return serverDescription;
		return { node: nodes[vars.path] ?? {sc: 0x806f0000} };
	} } );
	const { router, snackbar, forget, leaf } = await openRoute( {routes: [{ path: 'gateways/:gateway', component: Blank }, { path: 'gateways/:gateway/:connection', children: [
		{ path: '**', component: Blank, providers: [NodeResolver], resolve: {pageData: NodeResolver}, runGuardsAndResolvers: "pathParamsOrQueryParamsChange" }
	]}], providers: [
		{ provide: GATEWAY_SERVICE, useValue: {gateway: async ()=>{ if( gatewayDown ) throw new Error( "Failed to fetch" ); return gateway; }} },
		{ provide: OPC_STORE, useExisting: OpcStore },
		{ provide: ProfileStore, useValue: profileStoreStub() }
	], url, before: ()=>{
		const store = TestBed.inject( OpcStore );
		store.nodeView.set( <any>{sortNodes: ( n:UaNode[] )=>n} );
		if( browsed ){//the parent's page was open:  browseObjectsFolder stored its children, the Variable among them
			store.setNodes( 'gw', 'cn', OpcObject.rootNode, [pump1] );
			store.setNodes( 'gw', 'cn', pump1, [flow()] );
		}
	}} );
	return { router, exception: snackbar.exception, browseObjectsFolder, forget, postQL, pageData: leaf().data['pageData'] };
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
//The gateway's groups that hold the page's variables come with the page (#335), so the History tab is there when it shows;  a
//gateway that can't say - one older than the field, or one that refuses - leaves the page with the server's history alone.
describe( 'NodeResolver and the history groups', ()=>{
	it( 'asks for the groups of the page\'s variables, and carries them', async ()=>{
		const { exception, postQL, pageData } = await open( '/gateways/gw/cn/2~pump1', false, false, {groups: [{id: 3, name: 'Pumps', nodes: [{ns: 2, i: 11}]}]} );
		expect( exception ).not.toHaveBeenCalled();
		expect( postQL.mock.calls.map( ([, vars])=>vars ) ).toEqual( [{opc: 'cn', nodes: [{ns: 2, i: 11}]}] );
		expect( pageData.groups.map( ( g:any )=>[g.id, g.name, g.nodes.length] ) ).toEqual( [[3, 'Pumps', 1]] );
	} );
	it( 'keeps the page, with no groups, when the gateway refuses the question', async ()=>{
		const { exception, pageData } = await open( '/gateways/gw/cn/2~pump1', false, false, new Error( "Cannot query field historyGroups" ) );
		expect( exception ).not.toHaveBeenCalled();
		expect( pageData.nodes ).toHaveLength( 1 );
		expect( pageData.groups ).toEqual( [] );
	} );
} );

describe( 'NodeResolver on a node that is gone', ()=>{
	it( "forgets the page of a path the server does not have, and still goes up a level", async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/2~pump1/2~gone', false );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).toHaveBeenCalledWith( '/gateways/gw/cn/2~pump1/2~gone' );
		expect( router.url ).toBe( '/gateways/gw/cn/2~pump1' );
	} );

	it( 'keeps the page while the gateway is down', async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/2~pump1', false, true );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw' );//the level above, which the test's routes have to know of, or the redirect is an unhandled rejection
	} );
} );