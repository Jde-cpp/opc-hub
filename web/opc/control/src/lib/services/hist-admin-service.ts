import { Injectable } from '@angular/core';
import { HistPathSegment, histThresholdNames, HistThresholdOverride, histThresholdParams } from '../model/hist';
import { HistConnection, HistGroupMember, histNodeAttributeColumns, histNodeJson, HistRef, HistResolvedMember, HistTemplateGroup, HistTemplateMember, toHistGroupMember, toHistRef, toHistResolved, toHistTemplateMember } from '../model/hist-admin';
import { ExNodeId } from '../model/ex-node-id';
import { NodeId } from '../model/node-id';
import { Server } from '../model/server';
import { toBrowse } from '../model/types';
import { HistPoster, HistReader } from './history-service';

//The historian's tables for the admin pages (#337):  the lists a group's form picks from, each template's and group's members, and
//the member edits, each a table mutation the gateway routes to HistGroupMAwait for its live side, by POST as every mutation goes.
//The pages' own rows go through DetailPage's save, as a connection's does.
@Injectable({ providedIn: 'root' })
export class HistAdminService{
	//the connections a group can be made on:  the live ones, as the gateway's connection list has them
	async connections( reader:HistReader ):Promise<HistConnection[]>{
		const data = await reader.query<any>( HistAdminService.connectionsQuery );
		return ( <any[]>(data?.serverConnections ?? []) ).map( c=>({ id: Number( c.id ), slug: String( c.slug ), name: String( c.name ?? c.slug ) }) );
	}
	static readonly connectionsQuery = 'serverConnections( deleted: null ){ id name slug }';
	async templates( reader:HistReader ):Promise<HistRef[]>{
		const data = await reader.query<any>( HistAdminService.templatesQuery );
		return ( <any[]>(data?.histTemplates ?? []) ).map( toHistRef ).filter( t=>!!t ).map( t=>t! );
	}
	static readonly templatesQuery = 'histTemplates( deleted: null ){ id name }';
	async templateMembers( reader:HistReader, templateId:number ):Promise<HistTemplateMember[]>{
		const data = await reader.query<any>( HistAdminService.templateMembersQuery, {templateId} );
		return ( <any[]>(data?.histTemplateNodes ?? []) ).map( toHistTemplateMember );
	}
	static readonly thresholdColumns = histThresholdNames.join( ' ' );
	static readonly templateMembersQuery = `histTemplateNodes( histTemplateId: $templateId ){ id histTemplateId path ${HistAdminService.thresholdColumns} }`;
	//the groups made from a template, each with the connection and root the template is applied to.  Deleted ones too:  a member's
	//purge takes its node out of them as well, so a restore brings them back without it.
	async templateGroups( reader:HistReader, templateId:number ):Promise<HistTemplateGroup[]>{
		const data = await reader.query<any>( HistAdminService.templateGroupsQuery, {templateId} );
		return ( <any[]>(data?.histGroups ?? []) ).map( g=>({ id: Number( g.id ), name: String( g.name ?? '' ), slug: String( g.slug ?? '' ), connection: toHistRef( g.serverConnection ), root: g.root ?? undefined, deleted: g.deleted!=null }) );
	}
	static readonly templateGroupsQuery = 'histGroups( histTemplateId: $templateId ){ id name slug root deleted serverConnection{ id name } }';
	async groupMembers( reader:HistReader, groupId:number ):Promise<HistGroupMember[]>{
		const data = await reader.query<any>( HistAdminService.groupMembersQuery, {groupId} );
		return ( <any[]>(data?.histGroupNodes ?? []) ).map( toHistGroupMember );
	}
	//with what the node's server said of it (#379), which the tab shows beside the node once the collector has read it
	static readonly groupMembersQuery = `histGroupNodes( histGroupId: $groupId ){ id node histTemplateNodeId ${HistAdminService.thresholdColumns} ${histNodeAttributeColumns.join( ' ' )} }`;

	//A member added to a template:  its path from the group's root, and the thresholds it sets.  The gateway resolves it in each of
	//the template's groups before it answers;  a throw is its refusal - a path in two spellings is one member, so the same path again
	//is a Conflict.  Answers the row's id.
	async createTemplateMember( poster:HistPoster, templateId:number, path:HistPathSegment[], thresholds:HistThresholdOverride={} ):Promise<number>{
		const {ql, vars} = HistAdminService.createTemplateMemberMutation( templateId, path, thresholds );
		return HistAdminService.createdId( await poster.postQL<any>( ql, vars ), 'createHistTemplateNode' );
	}
	static createTemplateMemberMutation( templateId:number, path:HistPathSegment[], thresholds:HistThresholdOverride ):{ql:string; vars:Record<string,unknown>}{
		const t = histThresholdParams( thresholds );
		return { ql: `mutation createHistTemplateNode( histTemplateId: $templateId, path: $path${t.params} ){ id }`, vars: { templateId, path: JSON.stringify( path ), ...t.vars } };
	}
	//a member's own thresholds changed:  each column given, null clearing it so the group nodes inherit their group's again
	async updateTemplateMember( poster:HistPoster, id:number, changes:HistThresholdOverride ):Promise<void>{
		const t = histThresholdParams( changes );
		await poster.postQL<any>( `mutation updateHistTemplateNode( id: $id${t.params} )`, { id, ...t.vars } );
	}
	//a member removed:  a purge, since a member has no soft delete - each group node made from it goes first
	async purgeTemplateMember( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeHistTemplateNode( id: $id )', {id} );
	}
	//A node added to a group by hand, by its URI from the picker, or by the server's index as typed, which the gateway spells through
	//the connection.  A throw is the refusal:  a node another group on the connection holds is a Conflict naming that group.
	async createGroupMember( poster:HistPoster, groupId:number, node:ExNodeId, thresholds:HistThresholdOverride={} ):Promise<number>{
		const {ql, vars} = HistAdminService.createGroupMemberMutation( groupId, node, thresholds );
		return HistAdminService.createdId( await poster.postQL<any>( ql, vars ), 'createHistGroupNode' );
	}
	static createGroupMemberMutation( groupId:number, node:ExNodeId, thresholds:HistThresholdOverride ):{ql:string; vars:Record<string,unknown>}{
		const t = histThresholdParams( thresholds );
		return { ql: `mutation createHistGroupNode( histGroupId: $groupId, node: $node${t.params} ){ id }`, vars: { groupId, node: histNodeJson( node ), ...t.vars } };
	}
	//a node removed from its group:  a purge, which writes its NodeRemoved and frees the (connection, node) index for another group
	async purgeGroupMember( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeHistGroupNode( id: $id )', {id} );
	}
	//resolveHistory( group ){ nodes{ templateNode path node status holder{ id name } } } (hist.jsonnet):  retries the group's
	//template members that have no row yet, answering every member - resolved before, now, or not and why.  Needs Update on the group.
	async resolve( poster:HistPoster, groupId:number ):Promise<HistResolvedMember[]>{
		const data = await poster.postQL<any>( HistAdminService.resolveMutation, {group: groupId} );
		return toHistResolved( data?.resolveHistory );
	}
	static readonly resolveMutation = 'mutation resolveHistory( group: $group ){ nodes{ templateNode path node status holder{ id name } } }';
	//Groups' roots on one connection by their browse paths from Objects (NodeQLAwait's `path` on the id form), in one query:  names
	//only, as a member's path shows, without a segment's `ns~`.  The gateway takes a node by the server's index, so a root by URI goes
	//through the server's namespaces.  Undefined for a root in a namespace the server lacks, or one the gateway can't place.
	async rootPaths( reader:HistReader, slug:string, server:Pick<Server,'namespaces'>, roots:ExNodeId[] ):Promise<(string|undefined)[]>{
		const ids = roots.map( r=>{
			const ns = r.nsu ? server.namespaces.find( n=>n.uri==r.nsu )?.index : r.ns;
			return ns==undefined ? undefined : new NodeId( {...r.toJson(), ns} ).toJson();
		} );
		const known = ids.filter( id=>!!id );
		const data = known.length ? await reader.query<any>( HistAdminService.rootPathsQuery, {opc: slug, id: known} ) : undefined;
		const paths = <any[]>(data?.nodes ?? []);
		let i = 0;
		return ids.map( id=>{
			const path = id ? paths[i++]?.path : undefined;
			return typeof path=='string' ? ['Objects', ...(path ? path.split( '/' ).map( s=>toBrowse( s, undefined ).name ) : [])].join( '/' ) : undefined;
		} );
	}
	static readonly rootPathsQuery = 'nodes( opc:$opc, id:$id ){ path }';

	//a create's new id, from the row it answers - the stock op's list, or the object the hub answers
	static createdId( data:any, command:string ):number{
		const r = data?.[command];
		const row = Array.isArray( r ) ? r[0] : r;
		const id = Number( row?.id );
		if( !Number.isFinite( id ) || id<=0 )
			throw new Error( `${command} answered no id:  ${JSON.stringify( data )}` );
		return id;
	}
}