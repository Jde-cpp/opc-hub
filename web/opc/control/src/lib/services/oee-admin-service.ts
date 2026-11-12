import { Service } from '@angular/core';
import { HistRef, toHistRef } from '../model/hist-admin';
import { OeeMachineStatus, OeeMutationResult, OeeRole, OeeSignal, toOeeMachineStatus, toOeeMutationResult, toOeeSignal } from '../model/oee-admin';
import { HistPoster, HistReader } from './history-service';

//a group as a machine's form picks it and links to it
export type OeeGroup = HistRef & { slug:string };
//the columns of a signal an update may name
export type OeeSignalChanges = { histGroupNodeId?:number; running?:string|null; ratePeriod?:number };

//OEE's tables for the admin pages (#404, docs/oee/spec.md *Machines*, *Reads*):  the groups a machine's form picks from, a
//machine's signals and their edits, each a table mutation the gateway routes to OeeMAwait for its checks and its live side, by
//POST as every mutation goes, its status and recomputeOee.  The pages' own rows go through DetailPage's save, and a group's
//members are HistAdminService's, read as the group's page reads them.
@Service()
export class OeeAdminService{
	//the groups a machine can be made over:  the live ones.  One that has a machine already is the gateway's refusal, naming it.
	async groups( reader:HistReader ):Promise<OeeGroup[]>{
		const data = await reader.query<any>( OeeAdminService.groupsQuery );
		return ( <any[]>(data?.histGroups ?? []) ).map( g=>({ ...toHistRef( g )!, slug: String( g.slug ?? '' ) }) );
	}
	static readonly groupsQuery = 'histGroups( deleted: null ){ id name slug }';
	async signals( reader:HistReader, machineId:number ):Promise<OeeSignal[]>{
		const data = await reader.query<any>( OeeAdminService.signalsQuery, {machineId} );
		return ( <any[]>(data?.oeeMachineSignals ?? []) ).map( toOeeSignal );
	}
	static readonly signalsQuery = 'oeeMachineSignals( oeeMachineId: $machineId ){ id role histGroupNodeId running ratePeriod }';
	//none where the gateway answers no status
	async status( reader:HistReader, machineId:number ):Promise<OeeMachineStatus|undefined>{
		const data = await reader.query<any>( OeeAdminService.statusQuery, {machine: machineId} );
		return toOeeMachineStatus( data?.oeeMachineStatus );
	}
	static readonly statusQuery = 'oeeMachineStatus( machine: $machine ){ enabled calendar processed engine why state }';

	//A member of the machine's group given a role.  A throw is the refusal - a role the machine has, a Rate beside a Total - and the
	//answer carries the row's id and the warnings.
	async createSignal( poster:HistPoster, machineId:number, role:OeeRole, nodeId:number ):Promise<OeeMutationResult>{
		return toOeeMutationResult( await poster.postQL<any>( OeeAdminService.createSignalMutation, {machineId, role, node: nodeId} ) );
	}
	static readonly createSignalMutation = 'mutation createOeeMachineSignal( oeeMachineId: $machineId, role: $role, histGroupNodeId: $node ){ id warnings }';
	//a signal's member, a State's running values or a Rate's period changed:  each column given, a null `running` the default [true]
	async updateSignal( poster:HistPoster, id:number, changes:OeeSignalChanges ):Promise<OeeMutationResult>{
		const {ql, vars} = OeeAdminService.updateSignalMutation( id, changes );
		return toOeeMutationResult( await poster.postQL<any>( ql, vars ) );
	}
	static updateSignalMutation( id:number, changes:OeeSignalChanges ):{ql:string; vars:Record<string,unknown>}{
		const params = Object.keys( changes ).map( key=>`, ${key}: $${key}` ).join( '' );
		return { ql: `mutation updateOeeMachineSignal( id: $id${params} ){ recompute warnings }`, vars: {id, ...changes} };
	}
	//a signal has no soft delete:  it leaves by purge, which frees its member to leave the group
	async purgeSignal( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeOeeMachineSignal( id: $id )', {id} );
	}
	//recomputeOee( machine, start, end ){ machine start end } (oee.jsonnet):  the machine's rows over the span made again from its
	//group's history.  Answers the span once it is queued, not done.  Needs Update on the machines and Read on the group.
	async recompute( poster:HistPoster, machineId:number, start:Date, end:Date ):Promise<{start:Date; end:Date}>{
		const data = await poster.postQL<any>( OeeAdminService.recomputeMutation, {machine: machineId, start: start.toISOString(), end: end.toISOString()} );
		const span = data?.recomputeOee;
		if( !span?.start || !span?.end )
			throw new Error( `recomputeOee answered no span:  ${JSON.stringify( data )}` );
		return { start: new Date( span.start ), end: new Date( span.end ) };
	}
	static readonly recomputeMutation = 'mutation recomputeOee( machine: $machine, start: $start, end: $end ){ machine start end }';
}