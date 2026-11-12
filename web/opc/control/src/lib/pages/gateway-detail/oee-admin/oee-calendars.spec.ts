import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { AppService, CalendarService } from 'jde-framework';
import { OeeCalendarField, OeeCalendarList } from './oee-calendars';
import { stable } from 'jde-spa/testing';

const plant = [{id: 2, name: 'Plant', slug: 'plant', timeZone: 'UTC', dayStart: 360}];
//#409:  the Calendar a machine's and a line's forms share, over the list as values
const render = async ( inputs:Record<string,unknown> )=>{
	TestBed.configureTestingModule( {providers: [provideRouter( [] )]} );
	const fixture = TestBed.createComponent( OeeCalendarField );
	const set = async ( next:Record<string,unknown> )=>{
		for( const [name, value] of Object.entries( next ) )
			fixture.componentRef.setInput( name, value );
		await stable( fixture );
	};
	await set( {none: 'None - planned around the clock', ...inputs} );
	const hint = ()=>fixture.nativeElement.querySelector( 'mat-hint' );
	return { field: fixture.componentInstance, set, hint };
};

describe( 'OeeCalendarField', ()=>{
	it( "says what the calendar does, with its page once the calendars are read", async ()=>{
		const f = await render( {calendarId: 2, hint: "The line's shifts and days."} );
		expect( [f.hint().textContent.trim(), f.field.missing()] ).toEqual( ["The line's shifts and days.", 2] );//unread:  shown by its id, no page
		await f.set( {calendars: plant, url: ['/apps/gateways', 'g', 'calendars']} );
		expect( f.hint().querySelector( 'a' ).getAttribute( 'href' ) ).toBe( '/apps/gateways/g/calendars/plant' );
		expect( f.field.missing() ).toBeUndefined();
		await f.set( {calendarId: 7} );
		expect( [f.field.missing(), f.hint().querySelector( 'a' )] ).toEqual( [7, null] );//not in the list:  by its id
	} );
	it( 'has no hint with neither words nor a page', async ()=>{
		const f = await render( {calendars: plant, calendarId: undefined} );
		expect( f.hint() ).toBeNull();
	} );
} );
describe( 'OeeCalendarList', ()=>{
	it( "keeps a gateway's calendars where a read again fails, and drops them for another gateway's", async ()=>{
		const calendars = vi.fn( async ()=>plant );
		const list = new OeeCalendarList( <AppService><unknown>{registrations: async ()=>[{instanceName: 'app', programName: 'AppServer'}]}, <CalendarService><unknown>{calendars} );
		vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		await list.load( 'g' );
		expect( [list.link( 2 ), list.missing( 2 )] ).toEqual( [['/apps/appServers', 'app', 'calendars', 'plant'], undefined] );
		calendars.mockRejectedValueOnce( new Error( 'down' ) );
		await list.load( 'g' );
		expect( list.calendars() ).toEqual( plant );
		calendars.mockRejectedValueOnce( new Error( 'down' ) );
		await list.load( 'h' );
		expect( [list.calendars(), list.missing( 2 )] ).toEqual( [undefined, 2] );
	} );
} );