import { Component, computed, input, output, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { AppService, CalendarRef, CalendarService } from 'jde-framework';

//The application server's shift calendars as a machine's and a line's form pick from them (#404, #409), with the page they are
//a tab of, under which each has its own:  a hub's own page, else the application server's, found by its registration.
export type OeeCalendars = { calendars:CalendarRef[]; url?:string[] };
export async function readOeeCalendars( app:AppService, service:CalendarService, gatewaySlug:string ):Promise<OeeCalendars>{
	const [calendars, registrations] = await Promise.all( [service.calendars( app ), app.registrations()] );
	const registered = ( program:string, instanceName?:string )=>registrations.find( r=>r.programName==program && (instanceName===undefined || r.instanceName==instanceName) );
	const hub = !registered( 'OpcGateway', gatewaySlug ) && !!registered( 'OpcHub', gatewaySlug );//as GatewayDetail.isHub
	const appServer = registered( 'AppServer' )?.instanceName;
	return { calendars, url: hub ? ['/apps/gateways', gatewaySlug, 'calendars'] : appServer ? ['/apps/appServers', appServer, 'calendars'] : undefined };
}

//The calendars as a page holds them across its tabs, read for each row it opens:  another gateway's go at once, and the same
//gateway's stay where a read fails, the row's calendar showing by its id then.
export class OeeCalendarList{
	constructor( private app:AppService, private service:CalendarService ){}
	calendars = signal<CalendarRef[]|undefined>( undefined );//undefined until the application server has answered, or when it can't
	url = signal<string[]|undefined>( undefined );//the page the calendars are a tab of
	async load( gatewaySlug:string ):Promise<void>{
		if( gatewaySlug!=this.#slug ){
			this.calendars.set( undefined );
			this.url.set( undefined );
		}
		const read = ++this.#reads;
		this.#slug = gatewaySlug;
		try{
			const { calendars, url } = await readOeeCalendars( this.app, this.service, gatewaySlug );
			if( read!=this.#reads )
				return;
			this.calendars.set( calendars );
			this.url.set( url );
		}
		catch( e ){
			console.warn( 'Could not list the calendars.', e );
		}
	}
	link( id:number|undefined ):string[]|undefined{ return oeeCalendarLink( this.calendars(), this.url(), id ); }
	missing( id:number|undefined ):number|undefined{ return oeeCalendarMissing( this.calendars(), id ); }
	#slug:string|undefined;
	#reads = 0;
}
//the calendar's page, once the calendars are read and it is among them
const oeeCalendarLink = ( calendars:CalendarRef[]|undefined, url:string[]|undefined, id:number|undefined )=>{
	const slug = calendars?.find( c=>c.id==id )?.slug;
	return slug && url ? [...url, slug] : undefined;
};
//a calendar the list lacks - deleted on the application server, or the list unread
const oeeCalendarMissing = ( calendars:CalendarRef[]|undefined, id:number|undefined )=>id && !calendars?.some( c=>c.id==id ) ? id : undefined;

//A form's Calendar, over an OeeCalendarList's `calendars` and `url`:  `none` what no calendar means for the row, `hint` what the
//calendar does for it, and the calendar's page under it.  `picked` is the calendar chosen, undefined for none.  The list comes as
//values, so the page's template reads its signals.
@Component({
	selector: 'oee-calendar-field',
	template: `
		<mat-form-field subscriptSizing="dynamic">
			<mat-label>Calendar</mat-label>
			<mat-select [value]="calendarId() ?? 0" (selectionChange)="picked.emit( $event.value || undefined )">
				<mat-option [value]="0">{{ none() }}</mat-option>
				@for( c of calendars() ?? []; track c.id ){
					<mat-option [value]="c.id">{{ c.name }}</mat-option>
				}
				@if( missing(); as id ){
					<mat-option [value]="id">Calendar {{ id }}{{ calendars() ? ' - not found' : '' }}</mat-option>
				}
			</mat-select>
			@if( hint() || link() ){
				<mat-hint>{{ hint() }}@if( link(); as link ){@if( hint() ){&nbsp; }<a [routerLink]="link">Open the calendar</a>}</mat-hint>
			}
		</mat-form-field>`,
	styles: [':host{ display:block; } mat-form-field{ width:100%; }'],
	imports: [MatFormFieldModule, MatSelectModule, RouterLink]
})
export class OeeCalendarField{
	calendars = input<CalendarRef[]|undefined>();
	url = input<string[]|undefined>();
	calendarId = input<number|undefined>();
	none = input.required<string>();
	hint = input( '' );
	picked = output<number|undefined>();
	readonly missing = computed( ()=>oeeCalendarMissing( this.calendars(), this.calendarId() ) );
	readonly link = computed( ()=>oeeCalendarLink( this.calendars(), this.url(), this.calendarId() ) );
}