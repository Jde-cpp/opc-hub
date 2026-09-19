import { inject, Injectable } from '@angular/core';
import { CardStatusValue, counted, countRows, ICardStatus, plural } from 'jde-framework';
import { AccessService } from './access-service';

//the Access tile and page:  the users, groups and roles the signed-in user can see.  Without the rights the queries reject,
//and the tile keeps its static summary.
@Injectable( {providedIn: 'root'} )
export class AccessCardStatus implements ICardStatus{
	readonly url = '/access';
	async status():Promise<CardStatusValue>{
		const [users, groups, roles] = await Promise.all( ['users', 'groups', 'roles'].map(c=>countRows(this.#access, c)) );
		const rest = `${counted(groups, 'group')} · ${counted(roles, 'role')}`;
		return { label: 'Users', figure: users, detail: rest, summary: `${counted(users, 'user')} · ${rest}` };
	}
	#access = inject( AccessService );
}

//each /access card:  the rows in its list.  A resource row that is not deleted is one whose rights are enforced (the
//install ships them all deleted), so that card counts those.
@Injectable( {providedIn: 'root'} )
export class AccessCollectionStatus implements ICardStatus{
	readonly url = '/access/:collection';
	async status( url:string ):Promise<CardStatusValue>{
		const collection = url.split( '/' ).filter( s=>s.length ).at( -1 )!;
		const n = await countRows( this.#access, collection );
		const label = collection=='resources' ? 'enforced' : plural( n, collection.replace(/s$/, '') );
		return { label, figure: n, summary: `${n} ${label}` };
	}
	#access = inject( AccessService );
}
