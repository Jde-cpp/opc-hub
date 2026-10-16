import { inject, Injectable } from '@angular/core';
import { ISearchProvider, searchRank, SearchResult } from 'jde-spa';
import { AccessService } from './access-service';

type Kind = 'user'|'group'|'role'|'resource';
type Entity = { kind:Kind; name:string; slug:string; summary?:string; schema?:string };
type Row = { id:number; name:string; slug:string };

const icons:Record<Kind,string> = { user: 'person', group: 'group', role: 'badge', resource: 'lock' };//app.routes.ts' collection icons.
const collections:Record<Kind,string> = { user: 'users', group: 'groups', role: 'roles', resource: 'resources' };

//Users, groups, roles and resources for the navbar search, rendered and scoped as `user:alice` etc.  The four tables are small
//and the server's only portable filter (glob) is case-sensitive on sqlite, so they are fetched once and matched here.
//A resource's page is at its schema and slug - its slug alone repeats across schemas.
@Injectable({ providedIn: 'root' })
export class AccessSearchProvider implements ISearchProvider{
	readonly name = 'access';
	readonly prefixes:Kind[] = [ 'user', 'group', 'role', 'resource' ];
	#access = inject( AccessService );
	#entities?:Promise<Entity[]>;
	#loadedAt = 0;
	static readonly ttl = 60_000;//ms - re-fetch after this so additions (and a re-login as someone else) show up without a reload.

	async search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>{
		if( !text.length && !scope )
			return [];
		const entities = await this.#load();
		return entities
			.filter( e=>!scope || e.kind==scope )
			.map( e=>({ entity: e, rank: text.length ? searchRank(text, e.name, e.summary) : 0 }) )
			.filter( x=>x.rank<3 )
			.sort( (a,b)=>a.rank-b.rank || a.entity.name.localeCompare(b.entity.name) )
			.slice( 0, limit )
			.map( ({entity, rank})=>({
				title: entity.name,
				prefix: entity.kind,
				route: entity.kind=='resource' ? [ '/access', collections.resource, entity.schema!, entity.slug ] : [ '/access', collections[entity.kind], entity.slug ],
				icon: icons[entity.kind],
				summary: entity.summary,
				rank,
				source: this.name
			}) );
	}
	invalidate():void{ this.#entities = undefined; }

	#load():Promise<Entity[]>{
		if( this.#entities && Date.now()-this.#loadedAt<AccessSearchProvider.ttl )
			return this.#entities;
		this.#loadedAt = Date.now();
		return this.#entities = this.#query().catch( e=>{ this.#entities = undefined; throw e; } );//never cache a failure.
	}
	async #query():Promise<Entity[]>{
		const list = async ( kind:Kind )=>(await this.#access.queryArray<Row>( `${collections[kind]}(deleted:null){ id name slug }` )).map( r=>({ kind, name: r.name, slug: r.slug }) as Entity );
		const [users, groups, roles, resources] = await Promise.all( [ list('user'), list('group'), list('role'), this.#access.loadResources() ] );
		return [ ...users, ...groups, ...roles, ...resources.map( r=>({ kind: 'resource', name: r.name, slug: r.slug, summary: r.schema, schema: r.schema }) as Entity ) ];
	}
}
