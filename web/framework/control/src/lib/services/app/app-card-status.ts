import { inject, Injectable } from '@angular/core';
import { CardStatusValue, countRows, ICardStatus } from '../../pages/cards/card-status';
import { APP_SERVICE } from './app-service';

//the Applications tile and page:  how many service instances are connected to the AppServer - the cards /apps lists.
@Injectable( {providedIn: 'root'} )
export class AppCardStatus implements ICardStatus{
	readonly url = '/apps';
	async status():Promise<CardStatusValue>{
		const running = await countRows( this.#app, 'connections' );
		return { label: 'Services', figure: running, detail: 'running', summary: `${running} running`, warn: running==0 };
	}
	#app = inject( APP_SERVICE );
}
