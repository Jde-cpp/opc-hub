import { Component, input, model } from '@angular/core';
import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { Calendar, toCalendarException, toCalendarPattern, toCalendarShift } from '../../../model/calendar';
import { Mutation } from '../../../model/ql/mutation';
import { AppService } from '../../../services/app/app-service';
import { CalendarService } from '../../../services/calendar-service';
import { ConfirmDialog } from '../../../shared/confirm/confirm-dialog';
import { Properties } from '../../graphql/properties/properties';
import { CalendarDetail, sharedCalendarLine } from './calendar-detail';
import { CalendarExceptionDialog, CalendarExceptionDialogData } from './calendar-exception-dialog';
import { CalendarShiftDialog, CalendarShiftDialogData } from './calendar-shift-dialog';
import { flush } from 'jde-spa/testing';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';

//America/New_York with a 06:00 day start, as the server's suites:  EDT is UTC-4 in July, so a day starts at 10:00Z.
const NewYork = 'America/New_York';
const row = { id: 3, name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 360 };
const first = toCalendarPattern( {id: 4, calendarId: 3, effective: null, cycleDays: 7, cycleStart: null} );
const july = toCalendarPattern( {id: 5, calendarId: 3, effective: '2026-07-06T10:00:00Z', cycleDays: 14, cycleStart: '2026-07-01T10:00:00Z'} );
const gone = toCalendarPattern( {id: 6, calendarId: 3, effective: '2026-06-01T10:00:00Z', cycleDays: 7, cycleStart: null, deleted: '2026-06-02T00:00:00Z'} );
const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
const night = toCalendarShift( {id: 8, calendarPatternId: 4, name: 'Night', days: 31, start: 960, minutes: 480, breaks: null} );
const holiday = toCalendarException( {id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-04T10:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false} );
const oldHoliday = toCalendarException( {id: 10, calendarId: 3, name: 'Memorial Day', start: '2026-05-25T10:00:00Z', finish: '2026-05-26T10:00:00Z', isOpen: false, deleted: '2026-05-27T00:00:00Z'} );
const june = new Date( '2026-06-15T12:00:00Z' );
//the row's form, which reads a whole schema:  in the page's imports for a test that draws it, so the page is compiled over the stub
@Component( {selector: 'properties', template: ''} )
class PropertiesStub{ ctor = input<unknown>(); record = model<unknown>(); type = input<string>(); schema = input<unknown>(); order = input<string[]>(); readonlyFields = input<string[]>(); excludedColumns = input<string[]>(); }

//The page over stubs:  `mutations` reads what Save sent the application server, the calendar service records what the tabs ask and
//send, and the dialog stub answers a confirmation and holds what a form was opened with.  `render` lets the fixture's first change
//detection run ngOnInit and draw the page, where the rest drive the page by hand.
const create = async ( pageRow:any, {render=false, serverZone='America/Chicago' as string|null}={} )=>{
	const app = { mutate: vi.fn( async ( _m:Mutation[]|string ):Promise<unknown>=>({}) ) };
	const mutations = ()=>app.mutate.mock.calls.flatMap( ([m])=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	const service = {
		serverTimeZone: vi.fn( async ()=>serverZone ?? undefined ),
		patterns: vi.fn( async ( _app:unknown, _id:number, deleted=false )=>deleted ? [july, gone, first] : [july, first] ),
		shifts: vi.fn( async ( _app:unknown, patternId:number )=>patternId==4 ? [night, day] : [] ),
		exceptions: vi.fn( async ( _app:unknown, _id:number, deleted=false )=>deleted ? [holiday, oldHoliday] : [holiday] ),
		createShift: vi.fn( async ()=>{} ), updateShift: vi.fn( async ()=>{} ), purgeShift: vi.fn( async ()=>{} ),
		createException: vi.fn( async ()=>{} ), updateException: vi.fn( async ()=>{} ),
		remove: vi.fn( async ( ..._args:unknown[] )=>{} )
	};
	const dialog = dialogStub( true );//a confirmation's answer, and a form's:  saved
	const {data, snackbar, router} = configureDetailPage( pageRow, {providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the zoneless fixture's own first change detection would run ngOnInit a second time, onto the row #load nulled
		{ provide: AppService, useValue: app },
		{ provide: CalendarService, useValue: service },
		{ provide: MatDialog, useValue: dialog },
		{ provide: MAT_TABS_CONFIG, useValue: {animationDuration: '0ms'} }
	]} );
	if( render )
		TestBed.overrideComponent( CalendarDetail, {remove: {imports: [Properties]}, add: {imports: [PropertiesStub]}} );
	const fixture = TestBed.createComponent( CalendarDetail );
	const page = fixture.componentInstance;
	if( render )
		fixture.detectChanges();
	else
		page.ngOnInit();
	await flush( 3 );
	//the data the last dialog of `component` was opened with
	const opened = <T>( component:unknown ):T=>(<{data:T}>dialog.open.mock.calls.filter( ([c])=>c===component ).at( -1 )![1]).data;
	return { page, fixture, app, mutations, service, dialog, opened, snackbar, router, data };
};

describe( 'CalendarDetail', ()=>{
	//a new calendar:  the server's zone offered, midnight for its day start, and Save sends both with the name
	it( "offers the server's zone on a new calendar, and saves it with the day start", async ()=>{
		const d = await create( {} );
		expect( d.page.isNew ).toBe( true );
		expect( d.service.serverTimeZone ).toHaveBeenCalledWith( d.app );
		expect( d.service.patterns ).not.toHaveBeenCalled();//nothing under a row not saved
		expect( d.page.calendar.timeZone ).toBe( 'America/Chicago' );
		expect( d.page.dayStartText() ).toBe( '00:00' );
		expect( d.page.calendar.canSave ).toBe( false );
		d.page.properties.set( new Calendar( {...d.page.properties(), name: 'Plant', slug: 'plant'} ) );
		expect( d.page.calendar.canSave ).toBe( true );
		d.page.setDayStart( '06:00' );
		d.page.setTimeZone( NewYork );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['createCalendar( slug:"plant", name:"Plant", timeZone:"America/New_York", dayStart:360 )'] );
		expect( d.router.navigate ).toHaveBeenCalled();
	} );
	it( "lists the browser's zones with the server's and the row's, and keeps a zone picked first", async ()=>{
		const d = await create( {}, {serverZone: 'Mars/Olympus_Mons'} );
		expect( d.page.zones() ).toContain( 'America/New_York' );
		expect( d.page.zones() ).toContain( 'UTC' );//which V8's list leaves out
		expect( d.page.zones() ).toContain( 'Mars/Olympus_Mons' );//the server's refusal is the final word on a name
		expect( d.page.excludedFields ).toBe( d.page.excludedFields );//one array, not a new one for Properties each check
		TestBed.resetTestingModule();//the second page of this test
		const other = await create( {id: 3, name: 'Plant', slug: 'plant', timeZone: 'Etc/Plant', dayStart: 0} );
		expect( other.page.zones() ).toContain( 'Etc/Plant' );
		expect( other.page.zone() ).toBe( 'UTC' );//a zone the browser lacks:  the times show in UTC
		expect( other.page.zoneLacking() ).toBe( true );
		expect( other.service.serverTimeZone ).not.toHaveBeenCalled();//an existing row keeps its own
	} );
	it( 'leaves a new calendar without a zone where the server gives none', async ()=>{
		const d = await create( {}, {serverZone: null} );
		expect( d.page.calendar.timeZone ).toBeUndefined();
		d.page.properties.set( new Calendar( {...d.page.properties(), name: 'Plant', slug: 'plant'} ) );
		expect( d.page.calendar.canSave ).toBe( false );
	} );
	it( "can't save while the day start is blank, and sends a changed one alone", async ()=>{
		const d = await create( row );
		expect( d.page.dayStartText() ).toBe( '06:00' );
		d.page.setDayStart( '' );
		expect( d.page.dayStartText() ).toBe( '' );
		expect( d.page.calendar.canSave ).toBe( false );
		d.page.setDayStart( '07:00' );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['updateCalendar( id:3, dayStart:420 )'] );
	} );
	//a refusal shows as the form's error, not a toast, until the form is edited
	it( "says the server's refusal on the form", async ()=>{
		const d = await create( row );
		d.app.mutate.mockRejectedValueOnce( new Error( "timeZone 'Mars/Olympus_Mons' is not one the server knows." ) );
		d.page.setTimeZone( 'Mars/Olympus_Mons' );
		await d.page.onSubmitClick();
		expect( d.page.saveError() ).toBe( "timeZone 'Mars/Olympus_Mons' is not one the server knows." );
		expect( d.snackbar.exception ).not.toHaveBeenCalled();
		expect( d.router.navigate ).not.toHaveBeenCalled();
		d.page.setTimeZone( NewYork );//an edit answers it
		expect( d.page.saveError() ).toBeUndefined();
	} );

	//Pattern
	it( 'lists the patterns from the first, with the one in effect today selected and its shifts in the order of the day', async ()=>{
		const d = await create( row );
		expect( d.service.patterns ).toHaveBeenCalledWith( d.app, 3, false );
		expect( d.page.patterns.rows().map( p=>p.id ) ).toEqual( [4, 5] );
		expect( d.page.inEffect()?.id ).toBe( 5 );//today is after July's
		expect( d.page.selected()?.id ).toBe( 5 );
		expect( d.service.shifts ).toHaveBeenLastCalledWith( d.app, 5 );
		expect( d.page.patterns.rows().map( p=>[d.page.fromText( p ), d.page.cycleText( p ), d.page.stateText( p )] ) ).toEqual( [['The beginning', 'Weekly', 'Replaced'], ['2026-07-06', '14-day cycle, day 1 on 2026-07-01', 'In effect']] );
		d.page.today.set( june );
		expect( d.page.selected()?.id ).toBe( 4 );
		expect( d.page.patterns.rows().map( p=>d.page.stateText( p ) ) ).toEqual( ['In effect', 'Upcoming'] );
		await d.page.refreshShifts();
		expect( d.page.shifts.rows().map( s=>s.name ) ).toEqual( ['Day', 'Night'] );
		expect( d.page.shifts.rows().map( s=>[d.page.daysText( s ), d.page.startText( s ), d.page.lengthText( s ), d.page.breaksText( s ), d.page.endText( s )] ) ).toEqual( [
			['M T W T F · ·', '06:00', '8 h', '10:00 (30 min)', 'Ends 14:00'],
			['M T W T F · ·', '22:00', '8 h', '', 'Ends 06:00 the next day']
		] );
		expect( d.page.daysDetail( day ) ).toBe( 'Monday, Tuesday, Wednesday, Thursday, Friday' );
	} );
	it( "shows another pattern's shifts when it is picked, by its own cycle", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.select( july );
		expect( d.page.selected() ).toBe( july );
		expect( d.service.shifts ).toHaveBeenLastCalledWith( d.app, 5 );
		expect( d.page.shifts.rows() ).toEqual( [] );
		expect( d.page.daysText( {...day, days: 0b1110011} ) ).toBe( '1-2, 5-7' );//a rotation's day numbers
		const reads = d.service.shifts.mock.calls.length;
		await d.page.select( july );//the one showing:  nothing to read
		expect( d.service.shifts ).toHaveBeenCalledTimes( reads );
	} );
	it( 'reads the deleted patterns too when asked, and restores and purges one', async ()=>{
		const d = await create( row );
		await d.page.setShowDeletedPatterns( true );
		expect( d.service.patterns ).toHaveBeenLastCalledWith( d.app, 3, true );
		expect( d.page.patterns.rows().map( p=>[p.id, d.page.stateText( p )] ) ).toEqual( [[4, 'Replaced'], [6, 'Deleted'], [5, 'In effect']] );
		await d.page.deletePattern( gone );
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarPattern', 6, 'restore' );
		await d.page.purgePattern( gone );
		expect( d.dialog.open.mock.calls.at( -1 )![1]!.data ).toMatchObject( {title: 'Purge the pattern?', confirm: 'Purge', destructive: true} );
		expect( (<{message:string}>d.dialog.open.mock.calls.at( -1 )![1]!.data).message ).toContain( 'The pattern from 2026-06-01 and its shifts' );
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarPattern', 6, 'purge' );
		d.dialog.answer = false;//a purge not confirmed
		await d.page.purgePattern( gone );
		expect( d.service.remove ).toHaveBeenCalledTimes( 2 );
	} );
	//the server refuses the delete of a calendar's only pattern:  said on the tab, the rows as they were
	it( "deletes a pattern at once, and says the server's refusal on the tab", async ()=>{
		const d = await create( row );
		const reads = d.service.patterns.mock.calls.length;
		await d.page.deletePattern( july );
		expect( d.dialog.open ).not.toHaveBeenCalled();//a soft delete has Restore
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarPattern', 5, 'delete' );
		expect( d.service.patterns ).toHaveBeenCalledTimes( reads+1 );
		d.service.remove.mockRejectedValueOnce( new Error( "It is the calendar's only pattern." ) );
		await d.page.deletePattern( first );
		expect( d.page.patterns.error() ).toBe( "It is the calendar's only pattern." );
		expect( d.page.patterns.busy() ).toBe( false );
		expect( d.service.patterns ).toHaveBeenCalledTimes( reads+1 );
	} );

	it( 'takes today afresh on a refresh', async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		expect( d.page.inEffect()?.id ).toBe( 4 );
		await d.page.refreshPatterns();
		expect( d.page.inEffect()?.id ).toBe( 5 );
	} );
	it( "leaves no other pattern's shifts under a selection whose own can't be read", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		d.service.shifts.mockRejectedValueOnce( new Error( 'down' ) );
		await d.page.refreshShifts();
		expect( d.page.shifts.rows().map( s=>s.name ) ).toEqual( ['Day', 'Night'] );//the same pattern's:  still its own
		d.service.shifts.mockRejectedValueOnce( new Error( 'down' ) );
		d.page.selectedId.set( 5 );
		await d.page.refreshShifts();
		expect( d.page.shifts.rows() ).toEqual( [] );
		expect( d.page.shifts.error() ).toContain( 'down' );
	} );

	//Shifts:  each a table mutation of its own, through the form
	it( 'adds a shift to the selected pattern, on from the last', async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		const reads = d.service.shifts.mock.calls.length;
		await d.page.addShift();
		const form = d.opened<CalendarShiftDialogData>( CalendarShiftDialog );
		expect( form ).toMatchObject( {pattern: first, dayStart: 360, startAt: 0} );//Night ends with the day, so from its start
		expect( form.shift ).toBeUndefined();
		await form.save( {name: 'Late', days: 31, start: 480, minutes: 480, breaks: null} );
		expect( d.service.createShift ).toHaveBeenCalledWith( d.app, 4, {name: 'Late', days: 31, start: 480, minutes: 480, breaks: null} );
		expect( d.service.shifts ).toHaveBeenCalledTimes( reads+1 );//read again once saved
		d.page.shifts.rows.set( [day] );
		await d.page.addShift();
		expect( d.opened<CalendarShiftDialogData>( CalendarShiftDialog ).startAt ).toBe( 480 );
	} );
	it( "edits a shift with what changed alone, and doesn't read again on Cancel", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		d.dialog.answers.set( CalendarShiftDialog, false );
		const reads = d.service.shifts.mock.calls.length;
		await d.page.editShift( day );
		const form = d.opened<CalendarShiftDialogData>( CalendarShiftDialog );
		expect( form.shift ).toBe( day );
		await form.save( {name: 'Day', days: 31, start: 0, minutes: 450, breaks: null} );
		expect( d.service.updateShift ).toHaveBeenCalledWith( d.app, 7, {minutes: 450, breaks: null} );
		expect( d.service.shifts ).toHaveBeenCalledTimes( reads );
	} );
	it( 'removes a shift by a purge once confirmed, and says a refusal on the tab', async ()=>{
		const d = await create( row );
		d.dialog.answer = false;
		await d.page.removeShift( day );
		expect( d.dialog.open.mock.calls.at( -1 )![0] ).toBe( ConfirmDialog );
		expect( d.service.purgeShift ).not.toHaveBeenCalled();
		d.dialog.answer = true;
		await d.page.removeShift( day );
		expect( d.service.purgeShift ).toHaveBeenCalledWith( d.app, 7 );
		d.service.purgeShift.mockRejectedValueOnce( new Error( '(403)Forbidden' ) );
		await d.page.removeShift( day );
		expect( d.page.shifts.error() ).toBe( '(403)Forbidden' );
	} );

	//Exceptions
	it( "lists the exceptions on the calendar's wall clock, and the deleted ones when asked", async ()=>{
		const d = await create( row );
		expect( d.service.exceptions ).toHaveBeenCalledWith( d.app, 3, false );
		expect( d.page.exceptions.rows().map( e=>[e.name, d.page.clock( e.start ), d.page.clock( e.finish )] ) ).toEqual( [['Independence Day', '2026-07-04 06:00', '2026-07-05 06:00']] );
		await d.page.setShowDeletedExceptions( true );
		expect( d.service.exceptions ).toHaveBeenLastCalledWith( d.app, 3, true );
		expect( d.page.exceptions.rows().map( e=>e.name ) ).toEqual( ['Memorial Day', 'Independence Day'] );//by start
	} );
	it( 'adds an exception to the calendar, and edits one with what changed alone', async ()=>{
		const d = await create( row );
		await d.page.addException();
		const add = d.opened<CalendarExceptionDialogData>( CalendarExceptionDialog );
		expect( add ).toMatchObject( {zone: NewYork, dayStart: 360} );
		await add.save( {name: 'Shutdown', start: '2026-12-24T06:00:00', finish: '2026-12-27T06:00:00', isOpen: false} );
		expect( d.service.createException ).toHaveBeenCalledWith( d.app, 3, {name: 'Shutdown', start: '2026-12-24T06:00:00', finish: '2026-12-27T06:00:00', isOpen: false} );
		await d.page.editException( holiday );
		const edit = d.opened<CalendarExceptionDialogData>( CalendarExceptionDialog );
		expect( edit.exception ).toBe( holiday );
		await edit.save( {name: 'Independence Day', start: '2026-07-04T06:00:00', finish: '2026-07-06T06:00:00', isOpen: false} );
		expect( d.service.updateException ).toHaveBeenCalledWith( d.app, 9, {finish: '2026-07-06T06:00:00'} );
	} );
	it( 'deletes and restores an exception at once, and purges a deleted one once confirmed', async ()=>{
		const d = await create( row );
		await d.page.deleteException( holiday );
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarException', 9, 'delete' );
		await d.page.deleteException( oldHoliday );
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarException', 10, 'restore' );
		expect( d.dialog.open ).not.toHaveBeenCalled();
		await d.page.purgeException( oldHoliday );
		expect( d.dialog.open.mock.calls.at( -1 )![1]!.data ).toMatchObject( {title: 'Purge Memorial Day?', confirm: 'Purge'} );
		expect( d.service.remove ).toHaveBeenLastCalledWith( d.app, 'CalendarException', 10, 'purge' );
	} );

	//spec *Web*:  calendars are shared, and the application server can't see who plans by one
	it( "confirms the calendar's own Delete and Purge with the shared-calendar line", async ()=>{
		const d = await create( row );
		d.dialog.answer = false;
		await d.page.onDeleteClick();
		expect( d.dialog.open.mock.calls.at( -1 )![1]!.data ).toMatchObject( {title: 'Delete Plant?', confirm: 'Delete', destructive: true} );
		expect( (<{message:string}>d.dialog.open.mock.calls.at( -1 )![1]!.data).message ).toContain( sharedCalendarLine );
		expect( d.mutations() ).toEqual( [] );
		d.dialog.answer = true;
		await d.page.onDeleteClick();
		expect( d.mutations() ).toEqual( ['deleteCalendar(id:3)'] );
		await d.page.onPurgeClick();
		expect( (<{message:string}>d.dialog.open.mock.calls.at( -1 )![1]!.data).message ).toContain( sharedCalendarLine );
		expect( d.mutations() ).toEqual( ['deleteCalendar(id:3)', 'purgeCalendar(id:3)'] );
	} );
	it( 'restores a deleted calendar without asking', async ()=>{
		const d = await create( {...row, deleted: '2026-07-01T00:00:00Z'} );
		await d.page.onDeleteClick();
		expect( d.dialog.open ).not.toHaveBeenCalled();
		expect( d.mutations() ).toEqual( ['restoreCalendar(id:3)'] );
	} );
	//the router reuses the page from one calendar to the next:  nothing of the last one's is left behind
	it( "drops the last calendar's rows and selection when another opens", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.select( july );
		d.page.patterns.error.set( 'refused' );
		d.data.next( {pageData: {row: {}, routing: {}, schema: {enums: new Map()}}} );
		expect( d.page.isNew ).toBe( true );
		expect( [d.page.patterns.rows(), d.page.shifts.rows(), d.page.exceptions.rows()] ).toEqual( [[], [], []] );
		expect( d.page.selected() ).toBeUndefined();
		expect( d.page.patterns.error() ).toBeUndefined();
	} );

	it( 'goes to Properties to say a refused Save, which shows only there', async ()=>{
		const d = await create( row );
		d.page.tabIndex.set( 2 );
		d.app.mutate.mockRejectedValueOnce( new Error( 'The slug is taken.' ) );
		await d.page.onSubmitClick();
		expect( d.page.tabIndex() ).toBe( 0 );
		expect( d.page.saveError() ).toContain( 'The slug is taken.' );
	} );
	it( 'offers no exception edit for a zone the browser lacks, whose times the server would read in another', async ()=>{
		const d = await create( {...row, timeZone: 'Etc/Plant'}, {render: true} );
		d.page.tabIndex.set( 2 );
		d.fixture.detectChanges( false );
		await flush( 3 );
		d.fixture.detectChanges( false );
		const root:HTMLElement = d.fixture.nativeElement;
		expect( root.querySelector( '.tab-body .hint' )?.textContent ).toContain( "doesn't know Etc/Plant" );
		expect( root.querySelector<HTMLButtonElement>( '.tab-toolbar button' )?.disabled ).toBe( true );
		expect( root.querySelector<HTMLButtonElement>( '[aria-label="Edit this exception"]' )?.disabled ).toBe( true );
	} );

	const tabs = ( root:HTMLElement )=>[...root.querySelectorAll( '.mdc-tab__text-label' )].map( e=>e.textContent!.trim() );
	it( 'shows Properties alone on a new calendar, and its patterns and exceptions once saved', async ()=>{
		expect( tabs( (await create( {}, {render: true} )).fixture.nativeElement ) ).toEqual( ['Properties'] );
		TestBed.resetTestingModule();//the second page of this test
		expect( tabs( (await create( row, {render: true} )).fixture.nativeElement ) ).toEqual( ['Properties', 'Pattern', 'Exceptions'] );
	} );
	it( 'draws the patterns with their shifts, and the exceptions', async ()=>{
		const d = await create( row, {render: true} );
		const root:HTMLElement = d.fixture.nativeElement;
		const show = async ( index:number )=>{
			d.page.tabIndex.set( index );
			d.fixture.detectChanges( false );
			await flush( 3 );
			d.fixture.detectChanges( false );
		};
		const cells = ( selector:string )=>[...root.querySelectorAll( selector )].map( e=>e.textContent!.trim() );
		d.page.today.set( june );
		await d.page.refreshShifts();
		await show( 1 );
		expect( cells( 'mat-radio-button' ) ).toEqual( ['The beginning', '2026-07-06'] );
		expect( root.querySelector( 'mat-row.selected mat-radio-button' )?.textContent?.trim() ).toBe( 'The beginning' );
		expect( root.querySelector( '.shifts h3' )?.textContent ).toBe( 'Shifts from the beginning' );
		expect( [...root.querySelectorAll( '.shifts mat-row' )].map( r=>[...r.querySelectorAll( 'mat-cell:not(.mat-column-actions)' )].map( c=>c.textContent!.trim() ) ) ).toEqual( [
			['Day', 'M T W T F · ·', '06:00', '8 h', '10:00 (30 min)'],
			['Night', 'M T W T F · ·', '22:00', '8 h', 'none']
		] );
		d.page.shifts.error.set( "Shifts 'Day' on Monday and 'Late' on Monday overlap." );
		d.fixture.detectChanges( false );
		expect( root.querySelector( '.shifts [role=alert]' )?.textContent ).toContain( 'overlap' );
		await show( 2 );
		expect( cells( 'mat-row .mat-column-start, mat-row .mat-column-kind' ) ).toEqual( ['2026-07-04 06:00', 'Closed'] );
		await show( 0 );
		d.app.mutate.mockRejectedValueOnce( new Error( 'dayStart 1500 is not under 1440.' ) );
		d.page.setDayStart( '07:00' );
		await d.page.onSubmitClick();
		d.fixture.detectChanges( false );
		expect( root.querySelector( '.form-section [role=alert]' )?.textContent ).toContain( 'dayStart 1500' );
	} );
} );