import { ExNodeId } from '../model/ex-node-id';
import { HistAdminService } from './hist-admin-service';

//the service over a stub gateway:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });

describe( 'HistAdminService', ()=>{
	const service = new HistAdminService();
	it( 'lists the connections and templates a group picks from', async ()=>{
		const g = stub( {serverConnections: [{id: 1, name: 'Local', slug: 'local'}]} );
		expect( await service.connections( g ) ).toEqual( [{id: 1, slug: 'local', name: 'Local'}] );
		expect( g.query ).toHaveBeenCalledWith( 'serverConnections( deleted: null ){ id name slug }' );
		const t = stub( {histTemplates: [{id: 2, name: 'Pump'}]} );
		expect( await service.templates( t ) ).toEqual( [{id: 2, name: 'Pump'}] );
	} );
	it( "asks for a template's members and groups, and a group's nodes, by id", async ()=>{
		const g = stub( {histTemplateNodes: [{id: 3, histTemplateId: 2, path: '[{"nsu":"u","name":"a"}]'}]} );
		expect( (await service.templateMembers( g, 2 ))[0].path ).toEqual( [{nsu: 'u', name: 'a'}] );
		expect( g.query ).toHaveBeenCalledWith( 'histTemplateNodes( histTemplateId: $templateId ){ id histTemplateId path exceptionDeviation exceptionDeviationFormat minTimeInterval maxTimeInterval stepped }', {templateId: 2} );
		const groups = stub( {histGroups: [{id: 5, name: 'Pumps', slug: 'pumps', root: null, deleted: null, serverConnection: {id: 1, name: 'Local'}}, {id: 6, name: 'Old', slug: 'old', root: null, deleted: '2026-10-01T00:00:00Z', serverConnection: {id: 1, name: 'Local'}}]} );
		expect( await service.templateGroups( groups, 2 ) ).toEqual( [{id: 5, name: 'Pumps', slug: 'pumps', connection: {id: 1, name: 'Local'}, root: undefined, deleted: false}, {id: 6, name: 'Old', slug: 'old', connection: {id: 1, name: 'Local'}, root: undefined, deleted: true}] );
		expect( groups.query ).toHaveBeenCalledWith( 'histGroups( histTemplateId: $templateId ){ id name slug root deleted serverConnection{ id name } }', {templateId: 2} );//deleted ones too:  a member's purge reaches them
		const nodes = stub( {histGroupNodes: [{id: 7, node: '{"nsu":"u","i":1}', histTemplateNodeId: 3}]} );
		expect( (await service.groupMembers( nodes, 5 ))[0].templateNodeId ).toBe( 3 );
		expect( nodes.query ).toHaveBeenCalledWith( HistAdminService.groupMembersQuery, {groupId: 5} );
	} );
	//a create names only the thresholds given, and answers the row's id, from the object the hub answers or the list the stock op does
	it( 'creates a template member and a group node with the thresholds given', async ()=>{
		const m = HistAdminService.createTemplateMemberMutation( 2, [{nsu: 'u', name: 'a'}], {minTimeInterval: 1000, stepped: null} );
		expect( m.ql ).toBe( 'mutation createHistTemplateNode( histTemplateId: $templateId, path: $path, minTimeInterval: $minTimeInterval, stepped: $stepped ){ id }' );
		expect( m.vars ).toEqual( {templateId: 2, path: '[{"nsu":"u","name":"a"}]', minTimeInterval: 1000, stepped: null} );
		const n = HistAdminService.createGroupMemberMutation( 5, new ExNodeId( {ns: 2, i: 6012, nsu: 'urn:jde:pumps'} ), {} );
		expect( n.ql ).toBe( 'mutation createHistGroupNode( histGroupId: $groupId, node: $node ){ id }' );
		expect( n.vars ).toEqual( {groupId: 5, node: {nsu: 'urn:jde:pumps', i: 6012}} );
		expect( await service.createTemplateMember( stub( {createHistTemplateNode: {id: 7}} ), 2, [{nsu: 'u', name: 'a'}] ) ).toBe( 7 );
		expect( await service.createGroupMember( stub( {createHistGroupNode: [{id: 8}]} ), 5, new ExNodeId( {ns: 2, i: 1} ) ) ).toBe( 8 );
		await expect( service.createGroupMember( stub( {createHistGroupNode: []} ), 5, new ExNodeId( {ns: 2, i: 1} ) ) ).rejects.toThrow( 'answered no id' );
	} );
	//roots by browse path, in one query by the server's index:  one by URI through the namespaces, and one the server lacks or the
	//gateway can't place has none
	it( "places a connection's roots by their browse paths", async ()=>{
		const server = { namespaces: [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: 'urn:jde:pumps'}] };
		const g = stub( {nodes: [{path: 'pumps/5~pump1'}, {path: null}, {path: ''}]} );
		const roots = [new ExNodeId( {nsu: 'urn:jde:pumps', i: 5001} ), new ExNodeId( {nsu: 'urn:other', i: 1} ), new ExNodeId( {ns: 2, i: 9} ), new ExNodeId( {ns: 0, i: 85} )];
		expect( await service.rootPaths( g, 'local', server, roots ) ).toEqual( ['Objects/pumps/pump1', undefined, undefined, 'Objects'] );
		expect( g.query ).toHaveBeenCalledWith( 'nodes( opc:$opc, id:$id ){ path }', {opc: 'local', id: [{ns: 2, i: 5001}, {ns: 2, i: 9}, {ns: 0, i: 85}]} );
		const none = stub( {} );
		expect( await service.rootPaths( none, 'local', server, [roots[1]] ) ).toEqual( [undefined] );
		expect( none.query ).not.toHaveBeenCalled();
	} );
	it( 'updates, purges and resolves by id', async ()=>{
		const g = stub( {} );
		await service.updateTemplateMember( g, 3, {maxTimeInterval: 45000, exceptionDeviation: null} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateHistTemplateNode( id: $id, exceptionDeviation: $exceptionDeviation, maxTimeInterval: $maxTimeInterval )', {id: 3, exceptionDeviation: null, maxTimeInterval: 45000} );
		await service.purgeTemplateMember( g, 3 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeHistTemplateNode( id: $id )', {id: 3} );
		await service.purgeGroupMember( g, 7 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeHistGroupNode( id: $id )', {id: 7} );
		const r = stub( {resolveHistory: {nodes: [{templateNode: 3, path: '[]', node: null, status: 2153775104, holder: {id: 9, name: 'Other'}}]}} );
		const resolved = await service.resolve( r, 5 );
		expect( r.postQL ).toHaveBeenCalledWith( HistAdminService.resolveMutation, {group: 5} );
		expect( resolved[0].holder ).toEqual( {id: 9, name: 'Other'} );
	} );
} );