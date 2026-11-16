import { OeePlant, toOeeLineMember, toOeeLineRef } from '../model/oee-lines';
import type { HistReader } from './history-service';

//The gateway's live machines, by name, with its live lines and every membership (#406, #409):  what the dashboard orders its
//tiles and rows by, what a line's pages pick and link from, and what the floor's resolver draws the line's reach from (#481) -
//a function of its own, so the resolver, in the initial bundle, needs no page's service.  A caller who may not read the lines or
//their members has none of either, so every machine is one in no line;  `strict` throws instead, for a page that edits them.
export const oeeMachinesQuery = 'oeeMachines( deleted: null ){ id name slug histGroupId }';
export const oeeLinesQuery = 'oeeLines( deleted: null ){ id name slug level }';
export const oeeLineMembersQuery = 'oeeLineMembers{ id oeeLineId oeeMachineId memberLineId sort }';
export async function readOeePlant( reader:HistReader, strict=false ):Promise<OeePlant>{
	const lines = Promise.all( [reader.query<any>( oeeLinesQuery ), reader.query<any>( oeeLineMembersQuery )] ).then( ( [lines, members] )=>({
		lines: ( <any[]>(lines?.oeeLines ?? []) ).map( toOeeLineRef ), members: ( <any[]>(members?.oeeLineMembers ?? []) ).map( toOeeLineMember )
	}) );
	const [data, held] = await Promise.all( [reader.query<any>( oeeMachinesQuery ), strict ? lines : lines.catch( e=>{
		console.warn( "Could not read the lines' members.", e );
		return { lines: [], members: [] };
	} )] );
	return { machines: ( <any[]>(data?.oeeMachines ?? []) ).map( m=>({ id: Number( m.id ), name: String( m.name ?? m.slug ), slug: String( m.slug ), ...(m.histGroupId==null ? {} : {groupId: Number( m.histGroupId )}) }) ).sort( ( a, b )=>a.name.localeCompare( b.name ) ), ...held };
}