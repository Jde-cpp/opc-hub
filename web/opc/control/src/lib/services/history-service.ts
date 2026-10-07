import { Service } from '@angular/core';
import { Log } from 'jde-framework';
import { HistPage, HistReadArgs, HistSource, qlTime, toHistPage } from '../model/hist';

//What a read is sent through:  a Gateway, or a stub in a spec.
export type HistReader = { query<T>( ql:string, vars?:any, log?:Log ):Promise<T> };

//The one history service (plan Phase 4):  `hist` with `opc` for a server's own history, `group` for a gateway group (Phase 6) -
//the same arguments, result and continuation either way (spec *Pass-through*), so the trend and table never ask which.
@Service()
export class HistoryService{
	async read( reader:HistReader, source:HistSource, args:HistReadArgs, log:Log=()=>{} ):Promise<HistPage>{
		const {ql, vars} = HistoryService.query( source, args );
		const data = await reader.query<any>( ql, vars, log );
		return toHistPage( data?.["hist"] );
	}
	//hist( opc|group, nodes, start, end, limit, returnBounds, modified, continuation ){ continuation values{…} nodes{…} }
	//(apps/OpcGateway/config/introspection/hist.jsonnet) - only the arguments given, since the gateway reads an absent start or
	//end as an open end, and `modification` only with `modified`, the one mode that fills it.
	static query( source:HistSource, args:HistReadArgs ):{ql:string; vars:Record<string,unknown>}{
		const vars:Record<string,unknown> = source.group!=undefined ? {group: source.group} : {opc: source.opc};
		const params = [ source.group!=undefined ? "group: $group" : "opc: $opc", "nodes: $nodes" ];
		vars["nodes"] = args.nodes.map( n=>n.toJson() );
		const add = ( name:string, value:unknown )=>{ if( value!==undefined ){ params.push( `${name}: $${name}` ); vars[name] = value; } };
		add( "start", args.start ? qlTime(args.start) : undefined );
		add( "end", args.end ? qlTime(args.end) : undefined );
		add( "limit", args.limit );
		add( "returnBounds", args.bounds );
		add( "modified", args.modified );
		add( "continuation", args.continuation );
		const modification = args.modified ? " modification{ time type user }" : "";
		const ql = `hist( ${params.join(", ")} ){ continuation values{ node source server status value bound heartbeat${modification} } nodes{ node status } }`;
		return { ql, vars };
	}
}