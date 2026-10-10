import { Service } from '@angular/core';
import { Log } from 'jde-framework';
import { aggregateFunctionsFolder, editValueJson, HistAggregate, HistEditArgs, HistEditResult, histEditCommands, HistPage, HistReadArgs, HistSource, qlTime, toHistAggregates, toHistEditResult, toHistPage } from '../model/hist';
import { CnnctnSlug } from '../model/server-cnnctn';

//What a read is sent through:  a Gateway, or a stub in a spec.
export type HistReader = { query<T>( ql:string, vars?:any, log?:Log ):Promise<T> };
//What an edit is sent through:  a POST, since the edits are mutations and the gateway takes a mutation on no GET.
export type HistEditor = { postQL<T>( ql:string, vars?:any, log?:Log ):Promise<T> };

//The one history service:  `history` with `opc` for a server's own history, `group` for a gateway group once there are any -
//the same arguments, result and continuation either way (spec *Pass-through*), so the trend and table never ask which.
@Service()
export class HistoryService{
	async read( reader:HistReader, source:HistSource, args:HistReadArgs, log:Log=()=>{} ):Promise<HistPage>{
		const {ql, vars} = HistoryService.query( source, args );
		const data = await reader.query<any>( ql, vars, log );
		return toHistPage( data?.["history"] );
	}
	//history( opc|group, nodes, start, end, interval, aggregate, limit, returnBounds, modified, continuation ){ continuation values{…}
	//nodes{…} } (apps/OpcGateway/config/introspection/hist.jsonnet) - only the arguments given, since the gateway reads an absent
	//start or end as an open end and chooses the mode by the arguments, and `modification` only with `modified`, the one mode
	//that fills it.
	static query( source:HistSource, args:HistReadArgs ):{ql:string; vars:Record<string,unknown>}{
		const vars:Record<string,unknown> = source.group!=undefined ? {group: source.group} : {opc: source.opc};
		const params = [ source.group!=undefined ? "group: $group" : "opc: $opc", "nodes: $nodes" ];
		vars["nodes"] = args.nodes.map( n=>n.toJson() );
		const add = ( name:string, value:unknown )=>{ if( value!==undefined ){ params.push( `${name}: $${name}` ); vars[name] = value; } };
		add( "start", args.start ? qlTime(args.start) : undefined );
		add( "end", args.end ? qlTime(args.end) : undefined );
		add( "interval", args.interval );
		add( "aggregate", args.aggregate );
		add( "limit", args.limit );
		add( "returnBounds", args.bounds );
		add( "modified", args.modified );
		add( "continuation", args.continuation );
		const modification = args.modified ? " modification{ time type user }" : "";
		const ql = `history( ${params.join(", ")} ){ continuation values{ node source server status value bound heartbeat${modification} } nodes{ node status } }`;
		return { ql, vars };
	}
	//The aggregates a server lists, in its order:  the objects of its HistoryServerCapabilities/AggregateFunctions folder, which an
	//aggregate read names one of by browse name (spec *Pass-through*).  A server's own, so `opc` alone:  a group's are the
	//historian's, Phase 6's.  A server without the folder refuses the browse, which is the caller's to take as none listed.
	async aggregates( reader:HistReader, opc:CnnctnSlug, log:Log=()=>{} ):Promise<HistAggregate[]>{
		const data = await reader.query<any>( HistoryService.aggregatesQuery, {opc, id: aggregateFunctionsFolder.toJson()}, log );
		return toHistAggregates( data?.["node"] );
	}
	//the Children tab's browse (Gateway.browseObjectsFolder), down to what names an aggregate
	static readonly aggregatesQuery = "node( opc: $opc, id: $id ){ children{ id name browse nodeClass } }";
	//An edit (spec *Edits*), over the caller's own session, so the server's rules decide (spec *Pass-through*).  The result is
	//the server's answer per value and per node;  a refusal is in those statuses, not a throw, which is the gateway's for an
	//argument it can't take, a value of the wrong type among them.
	async edit( editor:HistEditor, source:HistSource, args:HistEditArgs, log:Log=()=>{} ):Promise<HistEditResult>{
		const {ql, vars, command} = HistoryService.mutation( source, args );
		const data = await editor.postQL<any>( ql, vars, log );
		return toHistEditResult( data?.[command] );
	}
	//createHistory|updateHistory|upsertHistory( opc|group, values:[{ node source server status value }] ), purgeHistory( opc|group,
	//nodes, start, end ) or purgeHistory( opc|group, nodes, times ), each { values{ node source status } nodes{ node status } }
	//(hist.jsonnet).  The result is keyed by the command, as every mutation's is.
	static mutation( source:HistSource, args:HistEditArgs ):{ql:string; vars:Record<string,unknown>; command:string}{
		const command = histEditCommands[args.kind];
		const vars:Record<string,unknown> = source.group!=undefined ? {group: source.group} : {opc: source.opc};
		const params = [ source.group!=undefined ? "group: $group" : "opc: $opc" ];
		if( args.kind=='purgeRange' || args.kind=='purgeTimes' ){
			params.push( "nodes: $nodes" );
			vars["nodes"] = args.nodes.map( n=>n.toJson() );
			if( args.kind=='purgeRange' ){
				params.push( "start: $start", "end: $end" );
				vars["start"] = qlTime( args.start );
				vars["end"] = qlTime( args.end );
			}
			else{
				params.push( "times: $times" );
				vars["times"] = args.times.map( qlTime );
			}
		}
		else{
			params.push( "values: $values" );
			vars["values"] = args.values.map( editValueJson );
		}
		const ql = `${command}( ${params.join(", ")} ){ values{ node source status } nodes{ node status } }`;
		return { ql, vars, command };
	}
}