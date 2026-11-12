import { Component, input, model } from '@angular/core';
import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { MAT_TABS_CONFIG } from '@angular/material/tabs';
import { By } from '@angular/platform-browser';
import { addDays, Calendar, mondayOf, productionDay, toCalendarException, toCalendarPattern, toCalendarShift } from '../../../model/calendar';
import { Mutation } from '../../../model/ql/mutation';
import { AppService } from '../../../services/app/app-service';
import { CalendarService } from '../../../services/calendar-service';
import { ConfirmDialog } from '../../../shared/confirm/confirm-dialog';
import { Properties } from '../../graphql/properties/properties';
import { CalendarChangeDialog, CalendarChangeDialogData } from './calendar-change-dialog';
import { CalendarCycleDialog, CalendarCycleDialogData } from './calendar-cycle-dialog';
import { CalendarDetail, sharedCalendarLine } from './calendar-detail';
import { CalendarExceptionDialog, CalendarExceptionDialogData } from './calendar-exception-dialog';
import { CalendarPreview } from './calendar-preview';
import { CalendarShiftDialog, CalendarShiftDialogData } from './calendar-shift-dialog';
import { flush } from 'jde-spa/testing';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';

//America/New_York with a 06:00 day start, as the server's suites:  EDT is UTC-4 in July, so a day starts at 10:00Z.
const NewYork = 'America/New_York';
const row = { id: 3, name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 360 };
const first = toCalendarPattern( {id: 4, calendarId: 3, effective: null, cycleDays: 7, cycleStart: null} );
const july = toCalendarPattern( {id: 5, calendarId: 3, effective: '2026-07-06T10:00:00Z', cycleDays: 14, cycleStart: '2026-07-01T10:00:00Z'} );
const gone = toCalendarPattern( {id: 6, calendarId: 3, effective: '2026-06-01T10:00:00Z', cycleDays: 7, cycleStart: null, deleted: '2026-06-02T00:00:00Z'} );
const august = toCalendarPattern( {id: 11, calendarId: 3, effective: '2026-08-03T10:00:00Z', cycleDays: 7, cycleStart: null} );//a copy of the first, from a Monday
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
		copyPattern: vi.fn( async ( ..._args:unknown[] )=>august ), updatePattern: vi.fn( async ( ..._args:unknown[] )=>{} ),
		occurrences: vi.fn( async ( ..._args:unknown[] )=>[] ),
		remove: vi.fn( async ( ..._args:unknown[] )=>{} )
	};
	const dialog = dialogStub( true );//a confirmation's answer, and a form's:  saved
	const {data, snackbar, router} = configureDetailPage( pageRow, {providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: render },//a rendered page detects changes itself, as an app does:  turned off, a zoneless tick inside flush() leaves the next detectChanges() nothing to refresh (NG0100).  One driven by hand doesn't, or its first change detection would run ngOnInit a second time, onto the row #load nulled
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
	afterEach( ()=>localStorage.clear() );//the tab a test left the page on

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
		//on the grid:  the week's columns, and a bar for each shift on each of its days
		expect( d.page.columns().map( c=>c.title ) ).toEqual( ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'] );
		expect( d.page.bars() ).toHaveLength( 10 );
		expect( d.page.bars().filter( b=>b.primary ).map( b=>b.detail ) ).toEqual( [
			'Day, Monday, Tuesday, Wednesday, Thursday, Friday, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)',
			'Night, Monday, Tuesday, Wednesday, Thursday, Friday, 22:00 to 06:00, 8 h'
		] );
	} );
	it( "shows another pattern's shifts when it is picked, by its own cycle", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.select( july );
		expect( d.page.selected() ).toBe( july );
		expect( d.service.shifts ).toHaveBeenLastCalledWith( d.app, 5 );
		expect( d.page.shifts.rows() ).toEqual( [] );
		expect( d.page.columns().map( c=>c.title ) ).toEqual( Array.from( {length: 14}, ( _, i )=>`Day ${i+1}` ) );//a rotation's day numbers
		expect( d.page.bars() ).toEqual( [] );
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

	//#397:  the date picker picks the pattern in effect
	it( 'shows the pattern in effect on the day picked, over one picked from the list, and today again for none', async ()=>{
		const d = await create( row );
		expect( [d.page.date(), d.page.dateText()] ).toEqual( [undefined, productionDay( d.page.today(), NewYork, 360 )] );
		const field = { value: '2026-06-15' };//a Monday in June, before July's pattern
		await d.page.setDate( field );
		expect( [d.page.date(), d.page.selected()?.id] ).toEqual( ['2026-06-15', 4] );
		expect( d.service.shifts ).toHaveBeenLastCalledWith( d.app, 4 );
		expect( d.page.columns().findIndex( c=>c.current ) ).toBe( 0 );
		expect( d.page.inEffect()?.id ).toBe( 5 );//the list's Status still says today's
		await d.page.select( july );//from the list
		expect( d.page.selected() ).toBe( july );
		expect( d.page.columns()[0] ).toEqual( {title: 'Day 1', short: 'Day 1', sub: 'Wed 06-03', current: false} );//the rotation's days under the picked day's cycle
		expect( d.page.columns()[12].current ).toBe( true );
		await d.page.setDate( field );//the date again:  the pattern in effect then, not the one picked
		expect( d.page.selected()?.id ).toBe( 4 );
		const reads = d.service.shifts.mock.calls.length;
		field.value = '2026-06-16';
		await d.page.setDate( field );//the same pattern's day:  nothing to read
		expect( d.service.shifts ).toHaveBeenCalledTimes( reads );
		field.value = '2026-07-06';
		await d.page.setDate( field );//the day July's begins
		expect( d.page.selected()?.id ).toBe( 5 );
		field.value = '';
		await d.page.setDate( field );
		expect( [d.page.date(), field.value] ).toEqual( [undefined, d.page.dateText()] );//the field shows today
		field.value = d.page.dateText();
		await d.page.setDate( field );//today, picked:  not a pick
		expect( d.page.date() ).toBeUndefined();
	} );
	//Change from… against a mocked service
	it( 'copies the pattern showing to a new one from a day, and shows the copy', async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		d.dialog.answers.set( CalendarChangeDialog, august );
		d.service.patterns.mockImplementation( async ()=>[july, august, first] );//once the copy is made
		await d.page.changeFrom();
		const form = d.opened<CalendarChangeDialogData>( CalendarChangeDialog );
		expect( form ).toMatchObject( {source: 'the beginning', zone: NewYork, from: addDays( productionDay( june, NewYork, 360 ), 1 )} );//tomorrow, with no day picked
		expect( await form.save( '2026-08-03' ) ).toBe( august );
		expect( d.service.copyPattern ).toHaveBeenCalledWith( d.app, 4, '2026-08-03T06:00:00' );//the day's start, with no zone
		expect( [d.page.selected()?.id, d.page.date()] ).toEqual( [11, '2026-08-03'] );
		expect( d.page.patterns.rows().map( p=>p.id ) ).toEqual( [4, 5, 11] );
		expect( d.service.shifts ).toHaveBeenLastCalledWith( d.app, 11 );
		await d.page.changeFrom();
		expect( d.opened<CalendarChangeDialogData>( CalendarChangeDialog ).from ).toBe( '2026-08-04' );//the copy has the day picked
	} );
	it( 'offers the day picked for the change, and reads nothing again on Cancel', async ()=>{
		const d = await create( row );
		await d.page.setDate( {value: '2026-12-07'} );
		d.dialog.answers.set( CalendarChangeDialog, undefined );
		const reads = d.service.patterns.mock.calls.length;
		await d.page.changeFrom();
		expect( d.opened<CalendarChangeDialogData>( CalendarChangeDialog ) ).toMatchObject( {source: '2026-07-06', from: '2026-12-07'} );
		expect( d.service.patterns ).toHaveBeenCalledTimes( reads );
		expect( [d.page.selected()?.id, d.page.date()] ).toEqual( [5, '2026-12-07'] );
	} );
	it( "sets the selected pattern's cycle with what changed alone", async ()=>{
		const d = await create( row );
		const reads = d.service.patterns.mock.calls.length;
		await d.page.editCycle();
		const form = d.opened<CalendarCycleDialogData>( CalendarCycleDialog );
		expect( form ).toMatchObject( {pattern: july, zone: NewYork, dayStart: 360, anchor: mondayOf( d.page.dateText() )} );
		await form.save( {cycleDays: 8, cycleStart: '2026-07-01T06:00:00'} );
		expect( d.service.updatePattern ).toHaveBeenCalledWith( d.app, 5, {cycleDays: 8} );//its anchor as it was
		expect( d.service.patterns ).toHaveBeenCalledTimes( reads+1 );
		d.dialog.answers.set( CalendarCycleDialog, false );
		await d.page.editCycle();
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
	//#397:  the grid's drag and its bars
	it( 'opens a new shift on the hours and the day dragged on the grid', async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		await d.page.drawShift( {day: 5, start: 240, minutes: 495} );
		const form = d.opened<CalendarShiftDialogData>( CalendarShiftDialog );
		expect( form ).toMatchObject( {pattern: first, dayStart: 360, startAt: 240, minutes: 495, days: [5]} );
		await form.save( {name: 'Saturday', days: 32, start: 240, minutes: 495, breaks: null} );
		expect( d.service.createShift ).toHaveBeenCalledWith( d.app, 4, {name: 'Saturday', days: 32, start: 240, minutes: 495, breaks: null} );
	} );
	it( "opens a bar's shift, and removes it from its form once confirmed", async ()=>{
		const d = await create( row );
		d.page.today.set( june );
		await d.page.refreshShifts();
		d.dialog.answers.set( CalendarShiftDialog, false );
		await d.page.openShift( 8 );
		expect( d.opened<CalendarShiftDialogData>( CalendarShiftDialog ).shift ).toBe( night );
		await d.page.openShift( 99 );//a bar of a shift since removed
		expect( d.dialog.open ).toHaveBeenCalledTimes( 1 );
		d.page.shifts.busy.set( true );//an edit is out:  the grid's bars and its drag wait for it
		await d.page.openShift( 8 );
		await d.page.drawShift( {day: 5, start: 240, minutes: 495} );
		expect( d.dialog.open ).toHaveBeenCalledTimes( 1 );
		d.page.shifts.busy.set( false );
		d.dialog.answers.set( CalendarShiftDialog, true );//saved, or removed
		const reads = d.service.shifts.mock.calls.length;
		await d.page.openShift( 7 );
		expect( d.service.shifts ).toHaveBeenCalledTimes( reads+1 );
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
	it( 'removes a shift by a purge once confirmed, over its form, which says a refusal', async ()=>{
		const d = await create( row );
		d.dialog.answers.set( CalendarShiftDialog, false );
		await d.page.editShift( day );
		const form = d.opened<CalendarShiftDialogData>( CalendarShiftDialog );
		d.dialog.answer = false;
		expect( await form.remove!() ).toBe( false );
		expect( d.dialog.open.mock.calls.at( -1 )![0] ).toBe( ConfirmDialog );
		expect( d.service.purgeShift ).not.toHaveBeenCalled();
		d.dialog.answer = true;
		expect( await form.remove!() ).toBe( true );
		expect( d.service.purgeShift ).toHaveBeenCalledWith( d.app, 7 );
		d.service.purgeShift.mockRejectedValueOnce( new Error( '(403)Forbidden' ) );
		await expect( form.remove!() ).rejects.toThrow( '(403)Forbidden' );
	} );
	it( 'lists a shift on no day under the grid, which has no bar for it', async ()=>{
		const d = await create( row, {render: true} );
		const idle = toCalendarShift( {id: 12, calendarPatternId: 4, name: 'Idle', days: 0, start: 0, minutes: 60, breaks: null} );
		d.page.today.set( june );
		d.service.shifts.mockImplementation( async ()=>[day, idle] );
		await d.page.refreshShifts();
		d.page.tabIndex.set( 1 );
		d.fixture.detectChanges( false );
		await flush( 3 );
		d.fixture.detectChanges( false );
		const link = (<HTMLElement>d.fixture.nativeElement).querySelector<HTMLButtonElement>( '.dayless button' )!;
		expect( link.textContent?.trim() ).toBe( 'Idle' );
		d.dialog.answers.set( CalendarShiftDialog, false );
		link.click();
		await flush( 2 );
		expect( d.opened<CalendarShiftDialogData>( CalendarShiftDialog ).shift ).toBe( idle );
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

	it( "holds back the date and a rotation's dates for a zone the browser lacks, which UTC would put a day out", async ()=>{
		const d = await create( {...row, timeZone: 'Etc/Plant'}, {render: true} );
		d.page.tabIndex.set( 1 );
		d.fixture.detectChanges( false );
		await flush( 3 );
		d.fixture.detectChanges( false );
		const root:HTMLElement = d.fixture.nativeElement;
		expect( root.querySelector( '.tab-body .hint' )?.textContent ).toContain( "a date can't be picked" );
		expect( root.querySelector<HTMLInputElement>( '.tab-toolbar input[type=date]' )?.disabled ).toBe( true );
		expect( d.page.columns() ).toHaveLength( 14 );
		expect( d.page.columns().every( c=>!c.sub && !c.current ) ).toBe( true );
	} );

	const tabs = ( root:HTMLElement )=>[...root.querySelectorAll( '.mdc-tab__text-label' )].map( e=>e.textContent!.trim() );
	it( 'shows Properties alone on a new calendar, and its patterns, exceptions and preview once saved', async ()=>{
		expect( tabs( (await create( {}, {render: true} )).fixture.nativeElement ) ).toEqual( ['Properties'] );
		TestBed.resetTestingModule();//the second page of this test
		expect( tabs( (await create( row, {render: true} )).fixture.nativeElement ) ).toEqual( ['Properties', 'Pattern', 'Exceptions', 'Preview'] );
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
		expect( root.querySelector( '.shifts h3' )?.textContent ).toBe( 'Shifts from the beginning, weekly' );
		expect( root.querySelector<HTMLInputElement>( '.tab-toolbar input[type=date]' )?.value ).toBe( d.page.dateText() );
		//the grid in place of the shifts table:  a bar for each shift on each of its days, which opens its form
		const bars = [...root.querySelectorAll<HTMLButtonElement>( '.shifts calendar-grid button.bar' )];
		expect( bars ).toHaveLength( 10 );
		expect( [...root.querySelectorAll( '.shifts calendar-grid .column' )[0].querySelectorAll( '.bar' )].map( b=>[b.querySelector( '.name' )?.textContent, b.querySelector( '.times' )?.textContent] ) ).toEqual( [['Day', '06:00 - 14:00'], ['Night', '22:00 - 06:00']] );
		expect( root.querySelector( '.shifts mat-table' ) ).toBeNull();
		d.dialog.answers.set( CalendarShiftDialog, false );
		bars[0].click();
		await flush( 2 );
		expect( d.opened<CalendarShiftDialogData>( CalendarShiftDialog ).shift ).toBe( day );
		d.page.shifts.error.set( "Shifts 'Day' on Monday and 'Late' on Monday overlap." );
		d.fixture.detectChanges( false );
		expect( root.querySelector( '.shifts [role=alert]' )?.textContent ).toContain( 'overlap' );
		await show( 2 );
		expect( cells( 'mat-row .mat-column-start, mat-row .mat-column-kind' ) ).toEqual( ['2026-07-04 06:00', 'Closed'] );
		await show( 3 );//the Preview reads its week when it is opened, with the closed exceptions for its bands
		expect( root.querySelector( 'calendar-preview calendar-grid' ) ).not.toBeNull();
		const exceptionReads = d.service.exceptions.mock.calls.length;
		await (<CalendarPreview>d.fixture.debugElement.query( By.directive( CalendarPreview ) ).componentInstance).reload();
		expect( d.service.exceptions ).toHaveBeenCalledTimes( exceptionReads+1 );//its Refresh, for the hatching
		const monday = mondayOf( productionDay( new Date(), NewYork, 360 ) );
		expect( d.service.occurrences ).toHaveBeenLastCalledWith( d.app, 3, `${monday}T06:00:00`, `${addDays( monday, 7 )}T06:00:00` );
		await show( 0 );
		d.app.mutate.mockRejectedValueOnce( new Error( 'dayStart 1500 is not under 1440.' ) );
		d.page.setDayStart( '07:00' );
		await d.page.onSubmitClick();
		d.fixture.detectChanges( false );
		expect( root.querySelector( '.form-section [role=alert]' )?.textContent ).toContain( 'dayStart 1500' );
	} );
} );