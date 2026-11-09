import { TestBed } from '@angular/core/testing';
import { CalendarException, CalendarOccurrence, toCalendarException, toCalendarOccurrence } from '../../../model/calendar';
import { CalendarService } from '../../../services/calendar-service';
import { CalendarPreview } from './calendar-preview';
import { flush } from 'jde-spa/testing';

//America/New_York under a 06:00 day start, on Wednesday 2026-07-01:  the week of Monday 2026-06-29, EDT
const NewYork = 'America/New_York';
const wednesday = new Date( '2026-07-01T15:00:00Z' );
const occurrence = ( shift:number|null, name:string, day:string, start:string, end:string )=>toCalendarOccurrence( {shift, name, day, start, end, breaks: []} );
//Friday's Day split by two hours of maintenance, and an extra Saturday
const week = [
	occurrence( 7, 'Day', '2026-06-29', '2026-06-29T10:00:00Z', '2026-06-29T18:00:00Z' ),
	occurrence( 7, 'Day', '2026-07-03', '2026-07-03T10:00:00Z', '2026-07-03T12:00:00Z' ),
	occurrence( 7, 'Day', '2026-07-03', '2026-07-03T14:00:00Z', '2026-07-03T18:00:00Z' ),
	occurrence( null, 'Extra Saturday', '2026-07-04', '2026-07-04T10:00:00Z', '2026-07-04T18:00:00Z' )
];
const maintenance = toCalendarException( {id: 1, calendarId: 3, name: 'Maintenance', start: '2026-07-03T12:00:00Z', finish: '2026-07-03T14:00:00Z', isOpen: false} );

//the Preview over a calendar service that answers `rows`, opened on that Wednesday, which `describe` sets the clock to
const create = async ( {rows=week, exceptions=[maintenance], lacking=<string|undefined>undefined}:{rows?:CalendarOccurrence[]; exceptions?:CalendarException[]; lacking?:string}={} )=>{
	const service = { occurrences: vi.fn( async ( ..._args:unknown[] )=>rows ) };
	TestBed.configureTestingModule({ providers: [{ provide: CalendarService, useValue: service }] });
	const fixture = TestBed.createComponent( CalendarPreview );
	const ql = { query: vi.fn() };
	for( const [name, value] of Object.entries( {ql, calendarId: 3, zone: lacking ? 'UTC' : NewYork, lacking, dayStart: 360, exceptions} ) )
		fixture.componentRef.setInput( name, value );
	const preview = fixture.componentInstance;
	const settle = async ()=>{ fixture.detectChanges(); await flush( 3 ); fixture.detectChanges(); };
	await settle();
	const root:HTMLElement = fixture.nativeElement;
	const bars = ()=>[...root.querySelectorAll<HTMLElement>( 'calendar-grid .bar' )].map( b=>[b.querySelector( '.name' )?.textContent, b.style.top, b.style.height] );
	return { fixture, preview, root, service, ql, settle, bars };
};

describe( 'CalendarPreview', ()=>{
	beforeEach( ()=>{ vi.setSystemTime( wednesday ); } );
	afterEach( ()=>{ vi.useRealTimers(); } );

	it( "asks for this week of production days in the calendar's zone, and draws it", async ()=>{
		const d = await create();
		expect( d.service.occurrences ).toHaveBeenCalledTimes( 1 );
		expect( d.service.occurrences ).toHaveBeenCalledWith( d.ql, 3, '2026-06-29T06:00:00', '2026-07-06T06:00:00' );
		expect( [d.preview.monday(), d.preview.sunday(), d.preview.today()] ).toEqual( ['2026-06-29', '2026-07-05', '2026-07-01'] );
		expect( [...d.root.querySelectorAll( 'calendar-grid .head .sub' )].map( e=>e.textContent ) ).toEqual( ['2026-06-29', '2026-06-30', '2026-07-01', '2026-07-02', '2026-07-03', '2026-07-04', '2026-07-05'] );
		expect( d.root.querySelector( 'calendar-grid .head.current .full' )?.textContent ).toBe( 'Wednesday' );
		expect( d.root.querySelector<HTMLInputElement>( 'input[type=date]' )?.value ).toBe( '2026-06-29' );
		expect( d.root.querySelector( '.count' )?.textContent ).toBe( '2026-06-29 to 2026-07-05' );
		expect( d.root.querySelector( 'button.bar' ) ).toBeNull();//a picture, not a form
	} );
	//the preview of an occurrence list with a split shift and an open Saturday
	it( 'draws a split shift in its two parts around the closed exception, and an open Saturday under its name', async ()=>{
		const d = await create();
		expect( d.bars() ).toEqual( [['Day', '0px', '224px'], ['Day', '0px', '56px'], ['Day', '112px', '112px'], ['Extra Saturday', '0px', '224px']] );
		const columns = [...d.root.querySelectorAll( 'calendar-grid .column' )];
		expect( columns.map( c=>c.querySelectorAll( '.bar' ).length ) ).toEqual( [1, 0, 0, 0, 2, 1, 0] );
		expect( columns[4].querySelector( '.band' )?.textContent?.trim() ).toBe( 'Maintenance' );
		expect( columns[5].querySelector( '.bar' )?.classList.contains( 'tone-3' ) ).toBe( true );
		expect( d.root.querySelector( '.empty' ) ).toBeNull();
	} );
	it( 'steps a week at a time, takes a date to its week and comes back to this one', async ()=>{
		const d = await create();
		d.service.occurrences.mockResolvedValueOnce( [] );
		d.preview.step( 1 );
		await d.settle();
		expect( d.service.occurrences ).toHaveBeenLastCalledWith( d.ql, 3, '2026-07-06T06:00:00', '2026-07-13T06:00:00' );
		expect( d.bars() ).toEqual( [] );
		expect( d.root.querySelector( '.empty' )?.textContent ).toBe( 'Nothing is planned in this week.' );
		expect( d.root.querySelector( 'calendar-grid .head.current' ) ).toBeNull();//today is not in it
		const field = { value: '2026-03-05' };//a Thursday
		d.preview.setWeek( field );
		expect( field.value ).toBe( '2026-03-02' );//the field shows the week's Monday
		await d.settle();
		expect( d.service.occurrences ).toHaveBeenLastCalledWith( d.ql, 3, '2026-03-02T06:00:00', '2026-03-09T06:00:00' );
		d.preview.step( -1 );
		expect( d.preview.monday() ).toBe( '2026-02-23' );
		const blank = { value: '' };
		d.preview.setWeek( blank );
		expect( [blank.value, d.preview.week()] ).toEqual( ['2026-06-29', undefined] );
	} );
	it( "says a refusal, keeping the week's own bars but no other week's", async ()=>{
		const d = await create();
		d.service.occurrences.mockRejectedValueOnce( new Error( 'down' ) );
		await d.preview.reload();
		d.fixture.detectChanges();
		expect( d.bars() ).toHaveLength( 4 );//the same week's:  still its own
		expect( d.root.querySelector( '[role=alert]' )?.textContent ).toContain( 'down' );
		d.service.occurrences.mockRejectedValueOnce( new Error( '(403)Forbidden' ) );
		d.preview.step( 1 );
		await d.settle();
		expect( d.bars() ).toEqual( [] );
		expect( d.root.querySelector( '[role=alert]' )?.textContent ).toContain( '(403)Forbidden' );
		expect( d.root.querySelector( '.empty' ) ).toBeNull();
	} );
	it( 'reads the week again on Refresh, and when the calendar or its day start changes', async ()=>{
		const d = await create();
		await d.preview.reload();
		expect( d.service.occurrences ).toHaveBeenCalledTimes( 2 );
		d.fixture.componentRef.setInput( 'dayStart', 0 );
		await d.settle();
		expect( d.service.occurrences ).toHaveBeenLastCalledWith( d.ql, 3, '2026-06-29T00:00:00', '2026-07-06T00:00:00' );
		d.fixture.componentRef.setInput( 'calendarId', 4 );
		await d.settle();
		expect( d.service.occurrences ).toHaveBeenLastCalledWith( d.ql, 4, '2026-06-29T00:00:00', '2026-07-06T00:00:00' );
		expect( d.service.occurrences ).toHaveBeenCalledTimes( 4 );
	} );
	it( 'tells the page of a Refresh, whose exceptions the hatching is', async ()=>{
		const d = await create();
		const reloaded = vi.fn();
		d.preview.reloaded.subscribe( reloaded );
		d.preview.step( 1 );
		await d.settle();
		expect( reloaded ).not.toHaveBeenCalled();
		await d.preview.reload();
		expect( reloaded ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'waits for a typed date to be finished, and takes a pick at once', async ()=>{
		const d = await create();
		const field = d.root.querySelector<HTMLInputElement>( 'input[type=date]' )!, reads = d.service.occurrences.mock.calls.length;
		field.dispatchEvent( new KeyboardEvent( 'keydown', {key: '1'} ) );
		field.value = '2026-03-05';
		field.dispatchEvent( new Event( 'change' ) );
		await d.settle();
		expect( [field.value, d.service.occurrences.mock.calls.length] ).toEqual( ['2026-03-05', reads] );
		field.dispatchEvent( new Event( 'blur' ) );
		await d.settle();
		expect( [field.value, d.preview.monday()] ).toEqual( ['2026-03-02', '2026-03-02'] );
		field.value = '2026-04-08';//the popup's:  no key to it
		field.dispatchEvent( new Event( 'change' ) );
		expect( d.preview.monday() ).toBe( '2026-04-06' );
	} );
	it( "draws nothing for a zone the browser lacks, whose wall clock it can't place", async ()=>{
		const d = await create( {lacking: 'Etc/Plant'} );
		expect( d.service.occurrences ).not.toHaveBeenCalled();
		expect( d.root.querySelector( 'calendar-grid' ) ).toBeNull();
		expect( d.root.querySelector( '.empty' )?.textContent ).toContain( "doesn't know Etc/Plant" );
	} );
} );