import { TestBed } from '@angular/core/testing';
import { ProfileStore } from 'jde-spa';
import { ENodeClass, Method, nodeColumns, OpcObject, UaNode, Variable, variableColumns } from '../../model/node';
import { GATEWAY_SERVICE } from '../gateway-service';
import { OPC_STORE, OpcStore } from '../opc-store';
import { NodeResolver } from './node-resolver';
import { Blank, profileStoreStub } from 'jde-spa/testing';
import { openRoute } from 'jde-framework/testing';
import { fakeGateway } from 'jde-opc/testing';

const objects = OpcObject.objectsNode;
const pump1 = new OpcObject( {ns: 2, i: 10, name: "pump1", browse: {ns: 2, name: "pump1"}} as any, objects );
const serverDescription = { connection: {id: 1, slug: 'cn', name: 'Line 1', url: 'opc.tcp://plc:4840', certificateUri: '', defaultBrowseNs: 1}, desc: {}, policy: '', mode: 'None', namespaces: [] };
const objectsParent = { ns: 0, i: 85, name: 'Objects', browse: {ns: 0, name: 'Objects'}, nodeClass: ENodeClass.Object, path: 'Objects' };
const pumpParent = { ns: 2, i: 10, name: 'pump1', browse: {ns: 2, name: 'pump1'}, nodeClass: ENodeClass.Object, path: 'Objects/2~pump1' };
const flowParent = { ns: 2, i: 11, name: 'flow', browse: {ns: 2, name: 'flow'}, nodeClass: ENodeClass.Variable, value: 7, historizing: true, path: 'Objects/2~pump1/2~flow' };
const nodes:Record<string, any> = {//the gateway's answer to node( path, root:"Root" ){ … nodeClass … parents{…} } per browse path from Root
	'Objects/2~pump1': { ns: 2, i: 10, name: 'pump1', browse: {ns: 2, name: 'pump1'}, nodeClass: ENodeClass.Object, parents: [objectsParent] },
	'Objects/2~pump1/2~flow': { ns: 2, i: 11, name: 'flow', browse: {ns: 2, name: 'flow'}, nodeClass: ENodeClass.Variable, value: 7, historizing: true, parents: [objectsParent, pumpParent] },
	'Objects/2~pump1/2~toHertz': { ns: 2, i: 12, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}, nodeClass: ENodeClass.Method, parents: [objectsParent, pumpParent] },
	'Objects/2~pump1/2~flow/EURange': { ns: 2, i: 13, name: 'EURange', browse: {ns: 0, name: 'EURange'}, nodeClass: ENodeClass.Variable, parents: [objectsParent, pumpParent, flowParent] }
};
const flow = ()=>new Variable( {ns: 2, i: 11, name: "flow", browse: {ns: 2, name: "flow"}, value: 7, historizing: true} as any, pump1 );
const toHertz = ()=>new Method( {ns: 2, i: 12, name: "toHertz", browse: {ns: 2, name: "toHertz"}} as any, pump1 );
const euRange = ()=>new Variable( {ns: 2, i: 13, name: "EURange", browse: {ns: 0, name: "EURange"}} as any );
//gatewayDown:  the gateway does not answer at all.  groups:  the gateway's answer to historyGroups, or an error it throws for it.
async function open( url:string, browsed:boolean, gatewayDown = false, groups:any = undefined ){
	const browseObjectsFolder = vi.fn( async ( _cnnctn:string, parent:UaNode )=>{
		if( parent.isVariable )
			return [euRange()];//its properties
		if( parent.isMethod )
			throw new EvalError( "Browsed a method as a page." );
		return groups ? [flow()] : [];
	} );
	const postQL = vi.fn( async ( ql:string, _vars:any )=>{
		if( !ql.startsWith("historyGroups") )
			throw new Error( `unexpected post: ${ql}` );
		if( groups instanceof Error )
			throw groups;
		return { historyGroups: groups };
	} );
	const query = vi.fn( async ( ql:string, vars:any )=>{
		if( ql.includes("serverDescription") )
			return serverDescription;
		return { node: nodes[vars.path] ?? {sc: 0x806f0000} };
	} );
	const querySingle = vi.fn( async ( _ql:string, _vars:any )=>({ value: 7, historizing: true }) );//the id form's read of a Variable's own columns
	const gateway = Object.assign( fakeGateway( 'gw' ), { browseObjectsFolder, postQL, errorCodeText: async ()=>"", query, querySingle } );
	const { router, snackbar, forget, leaf } = await openRoute( {routes: [{ path: 'gateways/:gateway', component: Blank }, { path: 'gateways/:gateway/:connection', children: [
		{ path: '**', component: Blank, providers: [NodeResolver], resolve: {pageData: NodeResolver}, runGuardsAndResolvers: "pathParamsOrQueryParamsChange" }
	]}], providers: [
		{ provide: GATEWAY_SERVICE, useValue: {gateway: async ()=>{ if( gatewayDown ) throw new Error( "Failed to fetch" ); return gateway; }} },
		{ provide: OPC_STORE, useExisting: OpcStore },
		{ provide: ProfileStore, useValue: profileStoreStub() }
	], url, before: ()=>{
		const store = TestBed.inject( OpcStore );
		store.nodeView.set( <any>{sortNodes: ( n:UaNode[] )=>n} );
		if( browsed ){//the parent's page was open:  browseObjectsFolder stored its children, the Variable and the Method among them
			store.setNodes( 'gw', 'cn', OpcObject.rootNode, [objects] );
			store.setNodes( 'gw', 'cn', objects, [pump1] );
			store.setNodes( 'gw', 'cn', pump1, [flow(), toHertz()] );
		}
	}} );
	return { router, exception: snackbar.exception, browseObjectsFolder, query, querySingle, forget, postQL, resolveEnum: gateway.resolveEnum, pageData: leaf().data['pageData'] };
}

//Phase 4 (#416, clients spec *Address space*):  a Variable has a page of its own - its value, status and tick first, then its
//HasProperty children - where reviews/m3-closing.md #9 sent it to its parent.  The url runs from Root, `Objects/…`, which the
//path query asks under `root: "Root"`.
describe( 'NodeResolver on a Variable', ()=>{
	it( 'shows the variable\'s page, itself first and its properties after, when the parent was browsed', async ()=>{
		const { router, exception, forget, browseObjectsFolder, pageData } = await open( '/gateways/gw/cn/Objects/2~pump1/2~flow', true );
		expect( exception ).not.toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/Objects/2~pump1/2~flow' );
		expect( browseObjectsFolder.mock.calls.map( c=>(c[1] as UaNode).name ) ).toEqual( ["flow"] );
		expect( pageData.nodes.map( ( n:UaNode )=>n.name ) ).toEqual( ["flow", "EURange"] );
		expect( pageData.route.node.isVariable ).toBe( true );
	} );

	//#13:  its own columns came by id after the path query, a round trip of their own
	it( "reads it and its own columns by its path from Root, in one query, when it was not", async ()=>{
		const { router, exception, query, querySingle, pageData } = await open( '/gateways/gw/cn/Objects/2~pump1/2~flow', false );
		expect( exception ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/Objects/2~pump1/2~flow' );
		const path = query.mock.calls.find( c=>c[0].includes( 'path:$path' ) )!;
		expect( path[0] ).toContain( 'root:"Root"' );
		expect( path[1] ).toEqual( {opc: 'cn', path: 'Objects/2~pump1/2~flow'} );
		expect( path[0] ).toContain( `${nodeColumns} ${variableColumns} parents{` );
		expect( querySingle ).not.toHaveBeenCalled();
		expect( pageData.nodes.map( ( n:UaNode )=>n.name ) ).toEqual( ["flow", "EURange"] );
		expect( (<Variable>pageData.nodes[0]).value ).toBe( 7 );
		expect( pageData.route.parent.path ).toBe( '/gateways/gw/cn/Objects/2~pump1' );//the sidenav's parent, through the Objects folder the store learned from `parents`
	} );

	//reviews-issue/416-address-space.md #10:  the page's own Variable kept its DataType's NodeId - browseObjectsFolder resolves only the
	//rows it browses - so a deep link's select had no values.  Resolved however the page was reached.
	it( "resolves the page's own Variable's enumeration on a deep link", async ()=>{
		const byUrl = await open( '/gateways/gw/cn/Objects/2~pump1/2~flow', false );
		expect( byUrl.resolveEnum.mock.calls.map( c=>[c[0], (c[1] as UaNode).name] ) ).toEqual( [['cn', 'flow']] );
		expect( byUrl.resolveEnum.mock.calls[0][1] ).toBe( byUrl.pageData.route.node );
	} );
	it( "resolves the page's own Variable's enumeration when the parent's page stored it", async ()=>{
		const browsed = await open( '/gateways/gw/cn/Objects/2~pump1/2~flow', true );
		expect( browsed.resolveEnum.mock.calls[0][1] ).toBe( browsed.pageData.route.node );
	} );

	//reviews-issue/416-address-space.md #3:  `parents` came without their class, so every parent was stored as an object - a
	//property's Variable, opened from the sidenav, browsed `children` and showed no row of its own and no History tab.
	it( "stores a Variable parent as a Variable, with its own columns", async ()=>{
		const { exception, query } = await open( '/gateways/gw/cn/Objects/2~pump1/2~flow/EURange', false );
		expect( exception ).not.toHaveBeenCalled();
		expect( query.mock.calls.find( c=>c[0].includes( 'path:$path' ) )![0] ).toContain( `parents{${nodeColumns} path ${variableColumns}}` );
		const stored = TestBed.inject( OpcStore ).findNodeId( 'gw', 'cn', 'Objects/2~pump1/2~flow' );
		expect( stored ).toBeInstanceOf( Variable );
		expect( (<Variable>stored).value ).toBe( 7 );
		expect( (<Variable>stored).historyReadable ).toBe( true );
		expect( TestBed.inject( OpcStore ).findNodeId( 'gw', 'cn', 'Objects/2~pump1' ) ).toBeInstanceOf( OpcObject );
	} );
} );

//reviews-issue/318-opc-calls.md #7:  a Method has no page - it is a Call on its object's row.  The browse stores methods
//now, so a stored one reached the resolver as a node, was browsed, and its argument properties listed as a page.
describe( 'NodeResolver on a Method', ()=>{
	it( 'shows the parent when the parent was browsed', async ()=>{
		const { router, exception, browseObjectsFolder } = await open( '/gateways/gw/cn/Objects/2~pump1/2~toHertz', true );
		expect( exception ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/Objects/2~pump1' );
		expect( browseObjectsFolder.mock.calls.map(c=>(c[1] as UaNode).name) ).not.toContain( "toHertz" );
	} );
	it( 'shows the parent when it was not', async ()=>{
		const { router, exception } = await open( '/gateways/gw/cn/Objects/2~pump1/2~toHertz', false );
		expect( exception ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw/cn/Objects/2~pump1' );
	} );
} );

//reviews/m3-closing.md #25:  a removed or renamed node's Recently visited tile stayed - the resolver redirects on failure, a
//NavigationCancel, not the NavigationError RecentVisits drops a page on.  It forgets the page itself when the server says the
//path is not there - not when the gateway or the OPC server is merely down, or an outage would empty every node tile.
//The gateway's groups that hold the page's variables come with the page (#335), so the History tab is there when it shows;  a
//gateway that can't say - one older than the field, or one that refuses - leaves the page with the server's history alone.
describe( 'NodeResolver and the history groups', ()=>{
	it( 'asks for the groups of the page\'s variables, and carries them', async ()=>{
		const { exception, postQL, pageData } = await open( '/gateways/gw/cn/Objects/2~pump1', false, false, {groups: [{id: 3, name: 'Pumps', nodes: [{ns: 2, i: 11}]}]} );
		expect( exception ).not.toHaveBeenCalled();
		expect( postQL.mock.calls.map( ([, vars])=>vars ) ).toEqual( [{opc: 'cn', nodes: [{ns: 2, i: 11}]}] );
		expect( pageData.groups.map( ( g:any )=>[g.id, g.name, g.nodes.length] ) ).toEqual( [[3, 'Pumps', 1]] );
	} );
	it( 'keeps the page, with no groups, when the gateway refuses the question', async ()=>{
		const { exception, pageData } = await open( '/gateways/gw/cn/Objects/2~pump1', false, false, new Error( "Cannot query field historyGroups" ) );
		expect( exception ).not.toHaveBeenCalled();
		expect( pageData.nodes ).toHaveLength( 1 );
		expect( pageData.groups ).toEqual( [] );
	} );
} );

describe( 'NodeResolver on a node that is gone', ()=>{
	it( "forgets the page of a path the server does not have, and still goes up a level", async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/Objects/2~pump1/2~gone', false );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).toHaveBeenCalledWith( '/gateways/gw/cn/Objects/2~pump1/2~gone' );
		expect( router.url ).toBe( '/gateways/gw/cn/Objects/2~pump1' );
	} );

	it( 'keeps the page while the gateway is down', async ()=>{
		const { router, exception, forget } = await open( '/gateways/gw/cn/Objects/2~pump1', false, true );
		expect( exception ).toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/gateways/gw' );//each level above resolves and fails the same way, down to the gateway's page, which the test's routes have to know of, or the redirect is an unhandled rejection
	} );
} );