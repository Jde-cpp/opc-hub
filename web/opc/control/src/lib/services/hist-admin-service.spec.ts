import { vi } from 'vitest';
import { ExNodeId } from '../model/ex-node-id';
import { HistAdminService } from './hist-admin-service';

//the service over a stub gateway:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });

describe( 'HistAdminService', ()=>{
	const service = new HistAdminService();
	it( 'lists the connections and templates a group picks from', async ()=>{
		const g = stub( {serverConnections: [{id: 1, name: 'Local', slug: 'local'}]} );
		expect( await service.connections( g ) ).toEqual( [{id: 1, slug: 'local', name: 'Local'}] );
		expect( g.query ).toHaveBeenCalledWith( 'serverConnections( deleted: null ){ id name slug }', undefined, expect.any( Function ) );
		const t = stub( {histTemplates: [{id: 2, name: 'Pump'}]} );
		expect( await service.templates( t ) ).toEqual( [{id: 2, name: 'Pump'}] );
	} );
	it( "asks for a template's members and groups, and a group's nodes, by id", async ()=>{
		const g = stub( {histTemplateNodes: [{id: 3, histTemplateId: 2, path: '[{"nsu":"u","name":"a"}]'}]} );
		expect( (await service.templateMembers( g, 2 ))[0].path ).toEqual( [{nsu: 'u', name: 'a'}] );
		expect( g.query ).toHaveBeenCalledWith( 'histTemplateNodes( histTemplateId: $templateId ){ id histTemplateId path exceptionDeviation exceptionDeviationFormat minTimeInterval maxTimeInterval stepped }', {templateId: 2}, expect.any( Function ) );
		const groups = stub( {histGroups: [{id: 5, name: 'Pumps', slug: 'pumps', root: null, serverConnection: {id: 1, name: 'Local'}}]} );
		expect( await service.templateGroups( groups, 2 ) ).toEqual( [{id: 5, name: 'Pumps', slug: 'pumps', connection: {id: 1, name: 'Local'}, root: undefined}] );
		const nodes = stub( {histGroupNodes: [{id: 7, node: '{"nsu":"u","i":1}', histTemplateNodeId: 3}]} );
		expect( (await service.groupMembers( nodes, 5 ))[0].templateNodeId ).toBe( 3 );
		expect( nodes.query ).toHaveBeenCalledWith( HistAdminService.groupMembersQuery, {groupId: 5}, expect.any( Function ) );
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
	it( 'updates, purges and resolves by id', async ()=>{
		const g = stub( {} );
		await service.updateTemplateMember( g, 3, {maxTimeInterval: 45000, exceptionDeviation: null} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateHistTemplateNode( id: $id, exceptionDeviation: $exceptionDeviation, maxTimeInterval: $maxTimeInterval )', {id: 3, exceptionDeviation: null, maxTimeInterval: 45000}, expect.any( Function ) );
		await service.purgeTemplateMember( g, 3 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeHistTemplateNode( id: $id )', {id: 3}, expect.any( Function ) );
		await service.purgeGroupMember( g, 7 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeHistGroupNode( id: $id )', {id: 7}, expect.any( Function ) );
		const r = stub( {resolveHistory: {nodes: [{templateNode: 3, path: '[]', node: null, status: 2153775104, holder: {id: 9, name: 'Other'}}]}} );
		const resolved = await service.resolve( r, 5 );
		expect( r.postQL ).toHaveBeenCalledWith( HistAdminService.resolveMutation, {group: 5}, expect.any( Function ) );
		expect( resolved[0].holder ).toEqual( {id: 9, name: 'Other'} );
	} );
} );