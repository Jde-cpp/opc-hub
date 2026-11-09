import { Service, Signal, computed, inject } from '@angular/core';
import { IProfileService } from 'jde-spa';
import { AppService } from './app/app-service';
import { Mutation, MutationType } from '../model/ql/mutation';
import { StringUtils } from '../utils/string-utils';

//Persists ProfileStore blobs to the AppServer's access_profiles table; the server scopes rows to the executer.
@Service()
export class ProfileService implements IProfileService{
	#app = inject( AppService );
	userKey:Signal<string|undefined> = computed( ()=>{
		const u = this.#app.user();//same logged-in predicate as ProtoService.isLoggedIn.
		return u && (u.jwt || u.id) ? (u.id ?? u.email) : undefined;
	} );
	async load( key:string ):Promise<string|null>{
		const row = await this.#app.querySingle<{value:string}|null>( `profile( url:${StringUtils.qlString(key)} ){ value }` );
		return row?.value ?? null;
	}
	//`prefix` is the start of a glob, and holds none of its `*`, `?` or `[`
	async loadRows( prefix:string ):Promise<Map<string,string>>{
		const rows = await this.#app.queryArray<{url:string, value:string}>( `profiles( url:{glob:${StringUtils.qlString(`${prefix}*`)}} ){ url value }` );
		return new Map( rows.map( row=>[row.url, row.value] ) );
	}
	async save( key:string, value:string|null ):Promise<void>{//value null deletes the row.
		await this.#app.mutate( new Mutation('profile', 0, {url:key, value:value}, MutationType.Update) );
	}
}