import { OeeAdminService } from './oee-admin-service';

//the service over a stub gateway:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });

describe( 'OeeAdminService', ()=>{
	const service = new OeeAdminService();
	it( 'lists the groups a machine picks from', async ()=>{
		const g = stub( {histGroups: [{id: 5, name: 'Pumps', slug: 'pumps'}]} );
		expect( await service.groups( g ) ).toEqual( [{id: 5, name: 'Pumps', slug: 'pumps'}] );
		expect( g.query ).toHaveBeenCalledWith( 'histGroups( deleted: null ){ id name slug }' );
	} );
	it( "asks for a machine's signals and its status by id", async ()=>{
		const g = stub( {oeeMachineSignals: [{id: 9, role: 'Rate', histGroupNodeId: 7, running: null, ratePeriod: 1000}]} );
		expect( await service.signals( g, 4 ) ).toEqual( [{id: 9, role: 'Rate', nodeId: 7, running: undefined, ratePeriod: 1000}] );
		expect( g.query ).toHaveBeenCalledWith( 'oeeMachineSignals( oeeMachineId: $machineId ){ id role histGroupNodeId running ratePeriod }', {machineId: 4} );
		const s = stub( {oeeMachineStatus: {enabled: true, calendar: 'found', processed: '2026-11-12T06:00:00Z', engine: 'live', why: null, state: 'stopped'}} );
		expect( await service.status( s, 4 ) ).toEqual( {enabled: true, calendar: 'found', processed: new Date( '2026-11-12T06:00:00Z' ), engine: 'live', why: undefined, state: 'stopped'} );
		expect( s.query ).toHaveBeenCalledWith( 'oeeMachineStatus( machine: $machine ){ enabled calendar processed engine why state }', {machine: 4} );
		expect( await service.status( stub( {} ), 4 ) ).toBeUndefined();
	} );
	it( 'creates a signal, answering its id and warnings', async ()=>{
		const g = stub( {createOeeMachineSignal: [{id: 9, warnings: ['Member 7 has an ExceptionDeviation of 5']}]} );
		expect( await service.createSignal( g, 4, 'Total', 7 ) ).toEqual( {id: 9, warnings: ['Member 7 has an ExceptionDeviation of 5'], recompute: false} );
		expect( g.postQL ).toHaveBeenCalledWith( 'mutation createOeeMachineSignal( oeeMachineId: $machineId, role: $role, histGroupNodeId: $node ){ id warnings }', {machineId: 4, role: 'Total', node: 7} );
	} );
	it( 'updates the columns given, answering recompute, and purges by id', async ()=>{
		const g = stub( {updateOeeMachineSignal: {recompute: true, warnings: ['running changed']}} );
		expect( await service.updateSignal( g, 9, {running: '[2,3]'} ) ).toEqual( {id: undefined, warnings: ['running changed'], recompute: true} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateOeeMachineSignal( id: $id, running: $running ){ recompute warnings }', {id: 9, running: '[2,3]'} );
		await service.updateSignal( g, 9, {histGroupNodeId: 8, ratePeriod: 1000} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateOeeMachineSignal( id: $id, histGroupNodeId: $histGroupNodeId, ratePeriod: $ratePeriod ){ recompute warnings }', {id: 9, histGroupNodeId: 8, ratePeriod: 1000} );
		await service.purgeSignal( g, 9 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeOeeMachineSignal( id: $id )', {id: 9} );
	} );
	it( 'recomputes a span, answering it as queued', async ()=>{
		const g = stub( {recomputeOee: {machine: 4, start: '2026-11-12T06:00:00.0000000Z', end: '2026-11-12T07:00:00.0000000Z'}} );
		const start = new Date( '2026-11-12T06:00:00Z' ), end = new Date( '2026-11-12T07:00:00Z' );
		expect( await service.recompute( g, 4, start, end ) ).toEqual( {start, end} );
		expect( g.postQL ).toHaveBeenCalledWith( 'mutation recomputeOee( machine: $machine, start: $start, end: $end ){ machine start end }', {machine: 4, start: '2026-11-12T06:00:00.000Z', end: '2026-11-12T07:00:00.000Z'} );
		await expect( service.recompute( stub( {} ), 4, start, end ) ).rejects.toThrow( 'answered no span' );
	} );
} );