import { Injectable, InjectionToken, signal, OnDestroy } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { IEnvironment } from 'jde-spa';
import { AppService, AuthStore, IGraphQL, StringUtils, TableSchema } from 'jde-framework';
import { Resource } from '../model/resource';
import { EffectiveRight, Names, UserRightsRow } from '../model/effective-right';
import { HolderLists, NamedRow, ResourceHolders, ResourceRightsRow } from '../model/resource-holder';



@Injectable( {providedIn: 'root'} )
export class AccessService extends AppService implements OnDestroy{
	ngOnDestroy(): void {
		console.log( 'AccessService.ngOnDestroy' );
	}

	//Cache the PROMISE, not the array.  The array was only assigned after the await, so two callers racing the first load
	//(every permission tab builds its own PermissionTable) both saw an unset #resources and both queried.
	//`refresh`, and every mutation (below), drop it:  each row carries its enforcement (`deleted`), and a list kept for the page's
	//life left Effective rights saying "nothing restricts this user" about a table the Enforced toggle had just closed - or a
	//lockout on one it had opened - until a reload (reviews/m3-closing.md #13).
	async loadResources( refresh=false ):Promise<Resource[]>{
		if( refresh )
			this.#resources = undefined;
		return this.#resources ??= this.#queryResources().catch( (e)=>{ this.#resources = undefined; throw e; } );//never cache a failure: a rejected promise would be handed to every later caller
	}
	//Any mutation through this service may change a resource - the Enforced toggle's delete/restoreResource, a role grant that
	//creates one (access_role_add) - so a landed one drops the list;  the next reader queries it again.
	override async mutate<Y>( ql:Parameters<AppService["mutate"]>[0], log?:Parameters<AppService["mutate"]>[1] ):Promise<Y>{
		const y = await super.mutate<Y>( ql, log );
		this.#resources = undefined;
		return y;
	}
	//Every resource, the node-scoped rows included:  a node a role is granted on is enforced for everyone, and a user holding
	//nothing on it is locked out of its subtree - Effective rights has to see it (reviews/m3-closing.md #14).  Not the cached
	//list:  PermissionTable wants the tables alone, and the tab wants the server's state now.
	async #queryAllResources():Promise<Resource[]>{
		return (await this.queryArray<Partial<Resource>>( `resources{ id schemaName allowed name deleted slug criteria }` )).map( (r)=>new Resource(r) );
	}
	async #queryResources():Promise<Resource[]>{
		const resources = (await this.queryArray<Partial<Resource>>( `resources(criteria:null){ id schemaName allowed name deleted slug description }` )).map( (r)=>new Resource(r) );//criteria:null - the node resources are known only through effectiveRights' rows
		this.#resourceSignal.set( resources );//the public `resources` signal was never written, so it read empty forever
		return resources;
	}
	//What the user ends up with on each resource through every path - the server's answer (userRights, Authorize::UserRights),
	//joined here to what its cache does not hold:  resource names/available rights from loadResources(), and the group and role
	//names the paths need, from the two lists.  The executer reads their own freely; another user's takes Read on acl.
	//The names only label the paths, so a list the user may not read (`roles` enforced, no Read on it) leaves them as `#id`
	//rather than failing the tab - the one place that would tell them why (reviews/m3-closing.md #28).  The rights and the
	//resources stay fatal:  a grid without them would read as "no rights".
	async effectiveRights( userId:number ):Promise<EffectiveRight[]>{
		return this.#rights( "userRights", userId, true );
	}
	//What a member gets through one group - the server's answer (groupRights, Authorize::GroupRights), in userRights' shape and
	//joined the same way, but with no lockout rows:  a resource the group gives nothing on is not a lockout for its members,
	//who may hold it elsewhere.  Takes Read on acl.
	async groupRights( groupId:number ):Promise<EffectiveRight[]>{
		return this.#rights( "groupRights", groupId, false );
	}
	async #rights( query:"userRights"|"groupRights", id:number, lockouts:boolean ):Promise<EffectiveRight[]>{
		const q = `${query}( id:$id ){ resource{ id schemaName slug criteria deleted } allowed denied effective sources{ permissionId allowed denied path{ id type } } }`;
		const nameList = ( collection:string ):Promise<{id:number, name:string}[]>=>this.queryArray<{id:number, name:string}>( `${collection}{ id name }` )
			.catch( e=>{ console.warn( `Effective rights:  no ${collection} names - the paths show ids.`, e ); return []; } );
		const [rows, resources, groups, roles] = await Promise.all( [
			this.queryArray<UserRightsRow>( q, {id} ),
			this.#queryAllResources(),//fresh - the tab is a diagnostic, the server's current enforcement whoever changed it - and the node rows too (#14)
			nameList( "groups" ),
			nameList( "roles" )
		] );
		const names:Names = { groups: new Map( groups.map(g=>[g.id, g.name]) ), roles: new Map( roles.map(r=>[r.id, r.name]) ) };
		return EffectiveRight.fromRows( rows, resources, names, lockouts );
	}
	//Everyone a resource's grants reach - the server's answer (resourceRights, Authorize::Holders) joined to the names and slugs
	//its cache does not hold.  The three lists only label the rows and link them, so one the user may not read leaves `#id`, as
	//effectiveRights' names do;  the answer itself stays fatal - an empty tab would read as "nobody".  Takes Read on acl.
	//The page passes the last answer's `lists`, read again only for a row they lack - one created since - not per resource.
	async resourceRights( resourceId:number, lists?:HolderLists ):Promise<ResourceHolders>{
		const sources = `sources{ permissionId allowed denied path{ id type } }`;
		const q = `resourceRights( id:$id ){ resource{ id deleted } roles{ id allowed denied ${sources} } groups{ id allowed denied ${sources} } users{ id allowed denied effective ${sources} } }`;
		let [row, named] = await Promise.all( [this.querySingle<ResourceRightsRow>( q, {id: resourceId} ), lists ?? this.#holderLists()] );
		if( lists && !ResourceHolders.covers(row, lists) )
			named = await this.#holderLists();
		return new ResourceHolders( row, named );
	}
	async #holderLists():Promise<HolderLists>{
		const list = ( collection:string ):Promise<NamedRow[]|undefined>=>this.queryArray<NamedRow>( `${collection}{ id name slug }` )
			.catch( e=>{ console.warn( `Resource rights:  no ${collection} names - the rows show ids.`, e ); return undefined; } );
		const [roles, groups, users] = await Promise.all( [list( "roles" ), list( "groups" ), list( "users" )] );
		return { roles, groups, users };
	}
	async getResource( slug:string ):Promise<Resource|undefined>{
		const resources = await this.loadResources();//was `await this.#resources` - the field, not the loader, so this threw on `.find` unless something else had already loaded
		return resources.find( r=>r.slug==slug );
	}

	//excludedColumns is the collection's tableSettings list, forwarded by DetailResolver.  Hardcoding [] here dropped it:
	//Role's introspected 'permissions' is the extends-base link (roles.role_id -> permissions.permission_id) and the
	//permissions table has no name column, so the generated `permissions{id name}` failed the whole query with a 500.
	override slugQuery( schema: TableSchema, slug: string, excludedColumns:string[]=[] ):string{
		let fields = super.fieldColumns( schema, excludedColumns );
		switch( schema.collectionName ){
			case "users":
				fields.push( `groups{id}` );
				break;
			case "groups":
				//the server parses/returns groupMembers (group_members view); 'members' is only the introspection field name.
				//It is in groupTableSettings.excludedColumns, so fieldColumns may not have emitted it at all - drop whatever
				//is there and append, rather than assigning over a findIndex of -1.
				fields = fields.filter( f=>!f.startsWith("members") );
				fields.push( `groupMembers(deleted:null){id isGroup}` );
				fields.push( "id" ); //not in the introspected type (the map table has two surrogate keys, so no pk field) but the subQueries need it.
				break;
			case "roles":
				fields.push( ...[`permissionRights{id allowed denied resource{id}}`, `roles{id}`] );
				break;
			default:
				throw new Error( `Unknown table: ${schema.collectionDisplay}` );
		}
		return `${schema.singular}( slug:${StringUtils.qlString(slug)} ){ ${fields.join(" ")} }`;
	}
	override subQueries( typeName: string, id: number ):string[]{
		let queries = new Array<string>();
		switch( typeName ){
		case "User":
		case "Group":
			queries = [
				`acl( identityId:${id} ){ permissionRights{id allowed denied resource{id deleted}} }`,
				`acl( identityId:${id} ){ role{id deleted} }`
			];
		break;
		case "Role":
			queries = [
				`acl( permissionId:${id} ){ identities(deleted:null){id isGroup} }`,
			];
		break;
		}
		return queries;
	}

	override toCollectionName( collectionDisplay:string ):string{
		return collectionDisplay;
	}


	#resources:Promise<Resource[]>|undefined;
	#resourceSignal = signal<Resource[]>(new Array<Resource>());
  resources = this.#resourceSignal.asReadonly();
};
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const ACCESS_SERVICE = new InjectionToken<AccessService>( 'AccessService' );
