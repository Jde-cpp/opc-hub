import { Component, inject, input, OnInit, signal } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { minutesToTime } from '../../../model/calendar';
import { AppService } from '../../../services/app/app-service';
import { QLListData, QLListResolver } from '../../../services/ql-list-resolver';
import { errorMessage } from '../../../utils/errors';
import { CellText } from '../../graphql/table/graphql-table';
import { QLList } from '../../ql/list/ql-list';
import { calendarListRoute, calendarsPath } from './calendar-list-route';

//The Calendars tab (#396) on the application server's page, and on a hub's, whose page is the gateway's:  a ql-list over the
//application server's `calendars`, resolved here when the tab opens as the gateway's historian lists are, and a row opens
//`calendars/<slug>` under the page (QLList.linkPrefix).  The rows go under `<url>/calendars` in the RouteStore, where the detail's
//resolver finds its siblings.
@Component({
	selector: 'calendar-list',
	template: `
		@if( listData(); as d ){
			<ql-list [listData]=d [ql]=app [linkPrefix]=linkPrefix [cellText]=cellText></ql-list>
		}
		@else if( error(); as e ){
			<div class="jde-empty" role="alert"><mat-icon aria-hidden="true">error_outline</mat-icon><p class="title">Could not open the calendars.</p><p class="detail">{{ e }}</p></div>
		}`,
	styles: ':host{ display: block; }',
	imports: [MatIconModule, QLList]
})
export class CalendarList implements OnInit{
	url = input.required<string>();//the page's url, apps/appServers/<instance>:  the key the rows go under, with `calendars`
	listData = signal<QLListData|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	app = inject( AppService );
	#profileStore = inject( ProfileStore );
	#prefs = inject( LocalPrefs );
	#routeStore = inject( RouteStore );
	readonly linkPrefix = [calendarsPath];
	async ngOnInit(){
		try{
			const data = await QLListResolver.data( this.app, calendarListRoute(), this.#profileStore, this.#prefs );
			const paged = { ...data, childrenKey: `${this.url()}/${calendarsPath}` };
			this.listData.set( await QLListResolver.loadOrFail( this.app, {...paged, skip: QLListResolver.savedSkip( paged )}, this.#routeStore ) );
		}
		catch( e ){//no schema:  the application server is down, or older than the calendar tables
			this.error.set( errorMessage(e) );
		}
	}
	readonly cellText:CellText = ( column, row )=>column=='dayStart' && row['dayStart']!=null ? minutesToTime( Number( row['dayStart'] ) ) : undefined;//the minutes as a time of day
}