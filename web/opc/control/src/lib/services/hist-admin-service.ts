import { Injectable } from '@angular/core';
import { Log } from 'jde-framework';
import { HistPathSegment, histThresholdNames, HistThresholdOverride } from '../model/hist';
import { HistConnection, HistGroupMember, histNodeJson, HistRef, HistResolvedMember, HistTemplateGroup, HistTemplateMember, toHistGroupMember, toHistRef, toHistResolved, toHistTemplateMember } from '../model/hist-admin';
import { ExNodeId } from '../model/ex-node-id';
import { HistPoster, HistReader } from './history-service';

//The historian's tables for the admin pages (#337):  the lists a group's form picks from, each template's and group's members, and
//the member edits, each a table mutation the gateway routes to HistGroupMAwait for its live side, by POST as every mutation goes.
//The pages' own rows go through DetailPage's save, as a connection's does.
@Injectable({ providedIn: 'root' })
export class HistAdminService{
	//the connections a group can be made on:  the live ones, as the gateway's connection list has them
	async connections( reader:HistReader, log:Log=()=>{} ):Promise<HistConnection[]>{
		const data = await reader.query<any>( HistAdminService.connectionsQuery, undefined, log );
		return ( <any[]>(data?.serverConnections ?? []) ).map( c=>({ id: Number( c.id ), slug: String( c.slug ), name: String( c.name ?? c.slug ) }) );
	}
	static readonly connectionsQuery = 'serverConnections( deleted: null ){ id name slug }';
	async templates( reader:HistReader, log:Log=()=>{} ):Promise<HistRef[]>{
		const data = await reader.query<any>( HistAdminService.templatesQuery, undefined, log );
		return ( <any[]>(data?.histTemplates ?? []) ).map( toHistRef ).filter( t=>!!t ).map( t=>t! );
	}
	static readonly templatesQuery = 'histTemplates( deleted: null ){ id name }';
	async templateMembers( reader:HistReader, templateId:number, log:Log=()=>{} ):Promise<HistTemplateMember[]>{
		const data = await reader.query<any>( HistAdminService.templateMembersQuery, {templateId}, log );
		return ( <any[]>(data?.histTemplateNodes ?? []) ).map( toHistTemplateMember );
	}
	static readonly thresholdColumns = histThresholdNames.join( ' ' );
	static readonly templateMembersQuery = `histTemplateNodes( histTemplateId: $templateId ){ id histTemplateId path ${HistAdminService.thresholdColumns} }`;
	//the groups made from a template, live ones, each with the connection and root the template is applied to
	async templateGroups( reader:HistReader, templateId:number, log:Log=()=>{} ):Promise<HistTemplateGroup[]>{
		const data = await reader.query<any>( HistAdminService.templateGroupsQuery, {templateId}, log );
		return ( <any[]>(data?.histGroups ?? []) ).map( g=>({ id: Number( g.id ), name: String( g.name ?? '' ), slug: String( g.slug ?? '' ), connection: toHistRef( g.serverConnection ), root: g.root ?? undefined }) );
	}
	static readonly templateGroupsQuery = 'histGroups( histTemplateId: $templateId, deleted: null ){ id name slug root serverConnection{ id name } }';
	async groupMembers( reader:HistReader, groupId:number, log:Log=()=>{} ):Promise<HistGroupMember[]>{
		const data = await reader.query<any>( HistAdminService.groupMembersQuery, {groupId}, log );
		return ( <any[]>(data?.histGroupNodes ?? []) ).map( toHistGroupMember );
	}
	static readonly groupMembersQuery = `histGroupNodes( histGroupId: $groupId ){ id node histTemplateNodeId ${HistAdminService.thresholdColumns} }`;

	//A member added to a template:  its path from the group's root, and the thresholds it sets.  The gateway resolves it in each of
	//the template's groups before it answers;  a throw is its refusal - a path in two spellings is one member, so the same path again
	//is a Conflict.  Answers the row's id.
	async createTemplateMember( poster:HistPoster, templateId:number, path:HistPathSegment[], thresholds:HistThresholdOverride={}, log:Log=()=>{} ):Promise<number>{
		const {ql, vars} = HistAdminService.createTemplateMemberMutation( templateId, path, thresholds );
		return HistAdminService.createdId( await poster.postQL<any>( ql, vars, log ), 'createHistTemplateNode' );
	}
	static createTemplateMemberMutation( templateId:number, path:HistPathSegment[], thresholds:HistThresholdOverride ):{ql:string; vars:Record<string,unknown>}{
		const t = HistAdminService.thresholdParams( thresholds );
		return { ql: `mutation createHistTemplateNode( histTemplateId: $templateId, path: $path${t.params} ){ id }`, vars: { templateId, path: JSON.stringify( path ), ...t.vars } };
	}
	//a member's own thresholds changed:  each column given, null clearing it so the group nodes inherit their group's again
	async updateTemplateMember( poster:HistPoster, id:number, changes:HistThresholdOverride, log:Log=()=>{} ):Promise<void>{
		const t = HistAdminService.thresholdParams( changes );
		await poster.postQL<any>( `mutation updateHistTemplateNode( id: $id${t.params} )`, { id, ...t.vars }, log );
	}
	//a member removed:  a purge, since a member has no soft delete - each group node made from it goes first
	async purgeTemplateMember( poster:HistPoster, id:number, log:Log=()=>{} ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeHistTemplateNode( id: $id )', {id}, log );
	}
	//A node added to a group by hand, by its URI from the picker, or by the server's index as typed, which the gateway spells through
	//the connection.  A throw is the refusal:  a node another group on the connection holds is a Conflict naming that group.
	async createGroupMember( poster:HistPoster, groupId:number, node:ExNodeId, thresholds:HistThresholdOverride={}, log:Log=()=>{} ):Promise<number>{
		const {ql, vars} = HistAdminService.createGroupMemberMutation( groupId, node, thresholds );
		return HistAdminService.createdId( await poster.postQL<any>( ql, vars, log ), 'createHistGroupNode' );
	}
	static createGroupMemberMutation( groupId:number, node:ExNodeId, thresholds:HistThresholdOverride ):{ql:string; vars:Record<string,unknown>}{
		const t = HistAdminService.thresholdParams( thresholds );
		return { ql: `mutation createHistGroupNode( histGroupId: $groupId, node: $node${t.params} ){ id }`, vars: { groupId, node: histNodeJson( node ), ...t.vars } };
	}
	//a node removed from its group:  a purge, which writes its NodeRemoved and frees the (connection, node) index for another group
	async purgeGroupMember( poster:HistPoster, id:number, log:Log=()=>{} ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeHistGroupNode( id: $id )', {id}, log );
	}
	//resolveHistory( group ){ nodes{ templateNode path node status holder{ id name } } } (hist.jsonnet):  retries the group's
	//template members that have no row yet, answering every member - resolved before, now, or not and why.  Needs Update on the group.
	async resolve( poster:HistPoster, groupId:number, log:Log=()=>{} ):Promise<HistResolvedMember[]>{
		const data = await poster.postQL<any>( HistAdminService.resolveMutation, {group: groupId}, log );
		return toHistResolved( data?.resolveHistory );
	}
	static readonly resolveMutation = 'mutation resolveHistory( group: $group ){ nodes{ templateNode path node status holder{ id name } } }';

	//the threshold columns an edit names, as `, name: $name` parameters and their variables, null for a clear
	static thresholdParams( changes:HistThresholdOverride ):{params:string; vars:Record<string,unknown>}{
		let params = '';
		const vars:Record<string,unknown> = {};
		for( const name of histThresholdNames ){
			if( name in changes ){
				params += `, ${name}: $${name}`;
				vars[name] = changes[name] ?? null;
			}
		}
		return { params, vars };
	}
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