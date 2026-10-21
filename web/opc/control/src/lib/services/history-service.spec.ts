import { aggregateFunctionsFolder } from '../model/hist';
import { HistoryService } from './history-service';
import { nodeA as A, nodeB as B } from 'jde-opc/testing';


describe( 'HistoryService.query', ()=>{
	it( 'reads a server\'s own history with opc, only the arguments given', ()=>{
		const {ql, vars} = HistoryService.query( {opc: 'local'}, {nodes: [A, B], end: new Date(1700000000500), limit: 1000} );
		expect( ql ).toBe( "history( opc: $opc, nodes: $nodes, end: $end, limit: $limit ){ continuation values{ node source server status value bound heartbeat } nodes{ node status } }" );
		expect( vars ).toEqual( {opc: 'local', nodes: [A.toJson(), B.toJson()], end: {seconds: 1700000000, nanos: 500000000}, limit: 1000} );
	} );
	it( 'reads a group with group, and asks for the modifications in modified mode', ()=>{
		const {ql, vars} = HistoryService.query( {group: 3}, {nodes: [A], start: new Date(1000), end: new Date(2000), modified: true, bounds: false, continuation: 'c'} );
		expect( ql ).toBe( "history( group: $group, nodes: $nodes, start: $start, end: $end, returnBounds: $returnBounds, modified: $modified, continuation: $continuation ){ continuation values{ node source server status value bound heartbeat modification{ time type user } } nodes{ node status } }" );
		expect( vars['group'] ).toBe( 3 );
		expect( vars['opc'] ).toBeUndefined();
		expect( vars['returnBounds'] ).toBe( false );//false is an argument, not an absence
		expect( vars['continuation'] ).toBe( 'c' );
	} );
	//an aggregate read:  both ends, the interval in milliseconds and the aggregate by name, which choose the mode (spec *Reads*)
	it( 'reads an aggregate over both ends with its interval', ()=>{
		const {ql, vars} = HistoryService.query( {opc: 'local'}, {nodes: [A], start: new Date(60_000), end: new Date(180_000), interval: 60_000, aggregate: 'Average', limit: 2} );
		expect( ql ).toBe( "history( opc: $opc, nodes: $nodes, start: $start, end: $end, interval: $interval, aggregate: $aggregate, limit: $limit ){ continuation values{ node source server status value bound heartbeat } nodes{ node status } }" );
		expect( vars ).toEqual( {opc: 'local', nodes: [A.toJson()], start: {seconds: 60, nanos: 0}, end: {seconds: 180, nanos: 0}, interval: 60_000, aggregate: 'Average', limit: 2} );
	} );
} );

describe( 'HistoryService.aggregates', ()=>{
	it( 'browses the AggregateFunctions folder of the connection and lists its objects', async ()=>{
		const query = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({node: {children: [
			{ id: {i: 2342}, name: {locale: 'en', text: 'Average'}, browse: {ns: 0, name: 'Average'}, nodeClass: 1 },
			{ id: {ns: 1, s: 'Median'}, name: {text: 'Median'}, browse: {ns: 1, name: 'Median'}, nodeClass: 1 }
		]}}) );
		const list = await new HistoryService().aggregates( {query}, 'local' );
		expect( query.mock.lastCall ).toEqual( [HistoryService.aggregatesQuery, {opc: 'local', id: aggregateFunctionsFolder.toJson()}] );
		expect( list ).toEqual( [{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}] );
	} );
	it( 'lists none when the gateway answers nothing', async ()=>{
		expect( await new HistoryService().aggregates( {query: async <T>()=><T><unknown>null}, 'local' ) ).toEqual( [] );
	} );
	//a History tab is made again on each return to it, and asks again:  the service browses once per gateway and connection
	it( 'browses once per gateway and connection, and again after a refusal', async ()=>{
		let refuse = true;
		const browse = vi.fn( async ( _name:string, vars:any ):Promise<any>=>{
			if( vars.opc=='bare' && refuse )
				throw new Error( 'BadNodeIdUnknown' );
			return {node: {children: [{ id: {i: 2342}, name: {text: 'Average'}, browse: {ns: 0, name: 'Average'}, nodeClass: 1 }]}};
		} );
		const reader = ( name:string )=>( {query: ( _ql:string, vars?:any )=>browse( name, vars )} );
		const sent = ()=>browse.mock.calls.map( ([name, vars])=>`${name}/${vars.opc}` );
		const service = new HistoryService(), a = reader( 'a' ), b = reader( 'b' );
		const [first, second] = await Promise.all( [service.aggregates( a, 'local' ), service.aggregates( a, 'local' )] );//one browse in flight, shared
		expect( second ).toBe( first );
		expect( await service.aggregates( a, 'local' ) ).toBe( first );
		await service.aggregates( a, 'other' );
		await service.aggregates( b, 'local' );//another gateway's connection of the same slug
		expect( sent() ).toEqual( ['a/local', 'a/other', 'b/local'] );

		await expect( service.aggregates( a, 'bare' ) ).rejects.toThrow( 'BadNodeIdUnknown' );
		refuse = false;
		expect( (await service.aggregates( a, 'bare' )).map( x=>x.browse ) ).toEqual( ['Average'] );
		await service.aggregates( a, 'bare' );
		expect( sent() ).toEqual( ['a/local', 'a/other', 'b/local', 'a/bare', 'a/bare'] );
	} );
} );

//historyGroups( opc, nodes ){ groups{ id name nodes } }:  which group holds each of a page's nodes (#335)
describe( 'HistoryService.groups', ()=>{
	it( 'asks the gateway by POST for the groups that hold the nodes, and parses each with its members', async ()=>{
		const postQL = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({historyGroups: {groups: [{id: 3, name: 'Pumps', nodes: [{ns: 2, i: 1}]}]}}) );
		const groups = await new HistoryService().groups( {postQL}, 'local', [A, B] );
		expect( postQL.mock.lastCall ).toEqual( [HistoryService.groupsQuery, {opc: 'local', nodes: [A.toJson(), B.toJson()]}] );
		expect( groups.map( g=>[g.id, g.name, g.nodes.map( n=>n.key )] ) ).toEqual( [[3, 'Pumps', [A.key]]] );
	} );
	it( 'asks nothing for no nodes, and lists none when the gateway answers nothing', async ()=>{
		const postQL = vi.fn( async ():Promise<any>=>null );
		expect( await new HistoryService().groups( {postQL}, 'local', [] ) ).toEqual( [] );
		expect( postQL ).not.toHaveBeenCalled();
		expect( await new HistoryService().groups( {postQL}, 'local', [A] ) ).toEqual( [] );
	} );
} );

//historyCapabilities( group ){ aggregates update delete }:  a group's aggregates, named alone, and this user's edit rights
describe( 'HistoryService.capabilities', ()=>{
	it( 'asks the gateway each time, and parses the aggregates as the picker lists them', async ()=>{
		const query = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({historyCapabilities: {aggregates: ['Average', 'Median'], update: true, delete: false}}) );
		const reader = { query };
		const service = new HistoryService();
		const c = await service.capabilities( reader, 3 );
		expect( c ).toEqual( {aggregates: [{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}], update: true, delete: false} );
		await service.capabilities( reader, 3 );
		expect( query.mock.calls ).toEqual( [[HistoryService.capabilitiesQuery, {group: 3}], [HistoryService.capabilitiesQuery, {group: 3}]] );
		expect( await service.capabilities( {query: async <T>()=><T><unknown>null}, 3 ) ).toEqual( {aggregates: [], update: false, delete: false} );
	} );
} );

describe( 'HistoryService.mutation', ()=>{
	//the three UpdateData fields, each with values:[{node source server status value}] (spec *Edits*)
	it( 'writes an UpdateData with its values, by its whole name', ()=>{
		const {ql, vars, command} = HistoryService.mutation( {opc: 'local'}, {kind: 'insert', values: [{node: A, source: new Date(1700000000500), value: 2.5}, {node: B, source: new Date(1000), status: 0x40000000, value: true}]} );
		expect( command ).toBe( 'createHistory' );
		expect( ql ).toBe( "createHistory( opc: $opc, values: $values ){ values{ node source status } nodes{ node status } }" );
		expect( vars ).toEqual( {opc: 'local', values: [{node: A.toJson(), source: {seconds: 1700000000, nanos: 500000000}, value: 2.5}, {node: B.toJson(), source: {seconds: 1, nanos: 0}, status: 0x40000000, value: true}]} );
		expect( HistoryService.mutation( {opc: 'local'}, {kind: 'replace', values: []} ).ql ).toMatch( /^updateHistory\( opc: \$opc, values: \$values \)/ );
		expect( HistoryService.mutation( {group: 3}, {kind: 'update', values: []} ).ql ).toMatch( /^upsertHistory\( group: \$group, values: \$values \)/ );
	} );
	it( 'purges a range with start and end, or at times, never both', ()=>{
		const range = HistoryService.mutation( {opc: 'local'}, {kind: 'purgeRange', nodes: [A, B], start: new Date(1000), end: new Date(2000)} );
		expect( range.command ).toBe( 'purgeHistory' );
		expect( range.ql ).toBe( "purgeHistory( opc: $opc, nodes: $nodes, start: $start, end: $end ){ values{ node source status } nodes{ node status } }" );
		expect( range.vars ).toEqual( {opc: 'local', nodes: [A.toJson(), B.toJson()], start: {seconds: 1, nanos: 0}, end: {seconds: 2, nanos: 0}} );
		const tick = {seconds: 1, nanos: 123400};//a row's time to the tick (historian-web-edits #1)
		expect( HistoryService.mutation( {opc: 'local'}, {kind: 'purgeRange', nodes: [A], start: tick, end: tick} ).vars ).toEqual( {opc: 'local', nodes: [A.toJson()], start: tick, end: tick} );
		const times = HistoryService.mutation( {opc: 'local'}, {kind: 'purgeTimes', nodes: [A], times: [new Date(1000), new Date(2500)]} );
		expect( times.ql ).toBe( "purgeHistory( opc: $opc, nodes: $nodes, times: $times ){ values{ node source status } nodes{ node status } }" );
		expect( times.vars ).toEqual( {opc: 'local', nodes: [A.toJson()], times: [{seconds: 1, nanos: 0}, {seconds: 2, nanos: 500000000}]} );
	} );
} );

describe( 'HistoryService.edit', ()=>{
	it( 'posts the mutation and reads the result under the command\'s name', async ()=>{
		const postQL = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({purgeHistory: {values: [], nodes: [{node: {ns:2, i:1}, status: 0x80B00000}]}}) );
		const result = await new HistoryService().edit( {postQL}, {opc: 'local'}, {kind: 'purgeRange', nodes: [A], start: new Date(1000), end: new Date(1000)} );
		expect( postQL.mock.lastCall![0] ).toMatch( /^purgeHistory\( opc: \$opc, nodes: \$nodes, start: \$start, end: \$end \)/ );
		expect( result.values ).toEqual( [] );
		expect( result.nodes.map( n=>[n.node.toString(), n.status] ) ).toEqual( [[A.toString(), 0x80B00000]] );
	} );
	it( 'is an empty result when the gateway answers nothing', async ()=>{
		const result = await new HistoryService().edit( {postQL: async <T>()=><T><unknown>null}, {opc: 'local'}, {kind: 'insert', values: [{node: A, source: new Date(1000), value: 1}]} );
		expect( result ).toEqual( {values: [], nodes: []} );
	} );
} );

describe( 'HistoryService.read', ()=>{
	it( 'sends the query through the reader and parses the page', async ()=>{
		const query = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({history: {continuation: null, values: [{node: {ns:2, i:1}, source: {seconds: 1, nanos: 0}, status: 0, value: 4}], nodes: [{node: {ns:2, i:1}, status: 0}]}}) );
		const page = await new HistoryService().read( {query}, {opc: 'local'}, {nodes: [A], end: new Date(5000)} );
		expect( query.mock.lastCall![0] ).toContain( 'history( opc: $opc, nodes: $nodes, end: $end )' );
		expect( page.continuation ).toBeNull();
		expect( page.values ).toHaveLength( 1 );
		expect( page.values[0].value ).toBe( 4 );
		expect( page.values[0].source?.getTime() ).toBe( 1000 );
	} );
	it( 'is an empty page when the gateway answers no data', async ()=>{
		const page = await new HistoryService().read( {query: async <T>()=><T><unknown>null}, {opc: 'local'}, {nodes: [A]} );
		expect( page ).toEqual( {values: [], continuation: null, nodes: []} );
	} );
} );

describe( 'HistoryService.thresholds', ()=>{
	it( 'asks historyThresholds for the group, with each threshold\'s source', async ()=>{
		const query = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({historyThresholds: {update: true, nodes: [{id: 7, node: {ns: 2, i: 1}, thresholds: [{name: 'stepped', value: true, source: 'default'}]}]}}) );
		const t = await new HistoryService().thresholds( {query}, 3 );
		expect( query.mock.lastCall ).toEqual( [HistoryService.thresholdsQuery, {group: 3}] );
		expect( t.update ).toBe( true );
		expect( t.nodes.map( n=>n.id ) ).toEqual( [7] );
	} );
	//updateHistGroupNode on the row, spelled as a mutation since the gateway takes no bare table verb:  only the columns changed,
	//in the columns' order, and null for one cleared back to inheriting
	it( 'overrides with the columns given and null for a clear', async ()=>{
		const {ql, vars} = HistoryService.overrideMutation( 7, {stepped: false, exceptionDeviation: null, minTimeInterval: 250} );
		expect( ql ).toBe( "mutation updateHistGroupNode( id: $id, exceptionDeviation: $exceptionDeviation, minTimeInterval: $minTimeInterval, stepped: $stepped )" );
		expect( vars ).toEqual( {id: 7, exceptionDeviation: null, minTimeInterval: 250, stepped: false} );
		const postQL = vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>({}) );
		await new HistoryService().override( {postQL}, 7, {exceptionDeviationFormat: 2} );
		expect( postQL.mock.lastCall ).toEqual( ["mutation updateHistGroupNode( id: $id, exceptionDeviationFormat: $exceptionDeviationFormat )", {id: 7, exceptionDeviationFormat: 2}] );
	} );
} );