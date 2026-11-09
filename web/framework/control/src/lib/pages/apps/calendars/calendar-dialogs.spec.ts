import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { toCalendarException, toCalendarShift } from '../../../model/calendar';
import { CalendarExceptionDialog, CalendarExceptionDialogData } from './calendar-exception-dialog';
import { CalendarShiftDialog, CalendarShiftDialogData } from './calendar-shift-dialog';
import { flush } from 'jde-spa/testing';

const NewYork = 'America/New_York';
const weekly = { cycleDays: 7 };
const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
const holiday = toCalendarException( {id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-04T10:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false} );

//a form over its data, with `save` recorded and the ref's close;  `type` sets a control as a user typing in it does
const open = <T extends {save:unknown}>( component:new()=>any, data:Omit<T,'save'>, save=vi.fn( async ( _args:unknown ):Promise<unknown>=>({}) ) )=>{
	const close = vi.fn( ( _y:unknown )=>{} );
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: {close} },
		{ provide: MAT_DIALOG_DATA, useValue: {...data, save} }
	]});
	const fixture = TestBed.createComponent( component );
	fixture.detectChanges();
	const root:HTMLElement = fixture.nativeElement;
	const settle = async ()=>{ await flush( 2 ); fixture.detectChanges(); };
	const type = async ( input:HTMLInputElement, value:string )=>{
		input.value = value;
		input.dispatchEvent( new Event( 'input' ) );
		await settle();
	};
	const saveButton = ()=><HTMLButtonElement>[...root.querySelectorAll( 'mat-dialog-actions button' )].at( -1 );
	return { fixture, dialog: fixture.componentInstance, root, save, close, settle, type, saveButton, inputs: ( selector='input' )=><HTMLInputElement[]>[...root.querySelectorAll( selector )] };
};

describe( 'CalendarShiftDialog', ()=>{
	it( "opens a new shift on the week's working days, eight hours from where the last ended", async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, startAt: 480} );
		await d.settle();
		expect( d.dialog.model() ).toEqual( {name: '', days: [true, true, true, true, true, false, false], start: '14:00', end: '22:00', breaks: []} );
		expect( d.inputs( 'input[type=time]' ).map( i=>i.value ) ).toEqual( ['14:00', '22:00'] );
		expect( [...d.root.querySelectorAll( 'mat-checkbox' )].map( c=>c.textContent!.trim() ) ).toEqual( ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'] );
		expect( d.inputs( 'mat-checkbox input' ).map( c=>c.checked ) ).toEqual( [true, true, true, true, true, false, false] );
		expect( d.dialog.lengthText() ).toBe( '8 h' );
		expect( d.saveButton().disabled ).toBe( true );//no name yet
		expect( d.root.querySelector( '.problem' ) ).toBeNull();//and nothing said of a form not touched
	} );
	it( "numbers a rotation's days, with none picked", async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: {cycleDays: 4, cycleStart: new Date( '2026-07-01T10:00:00Z' )}, dayStart: 360} );
		await d.settle();
		expect( [...d.root.querySelectorAll( 'mat-checkbox' )].map( c=>c.textContent!.trim() ) ).toEqual( ['Day 1', 'Day 2', 'Day 3', 'Day 4'] );
		expect( d.dialog.model().days ).toEqual( [false, false, false, false] );
		expect( d.dialog.model().start ).toBe( '06:00' );
	} );
	it( 'saves what was typed as the columns, and closes', async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360} );
		await d.settle();
		const [name] = d.inputs( 'input:not([type])' );
		await d.type( name, 'Night' );
		const [start, end] = d.inputs( 'input[type=time]' );
		await d.type( start, '22:00' );
		await d.type( end, '06:00' );
		expect( d.dialog.lengthText() ).toBe( '8 h, into the next day' );
		d.inputs( 'mat-checkbox input' )[5].click();//Saturday too
		d.dialog.addBreak();
		await d.settle();
		expect( d.inputs( 'input[type=time]' )[2].value ).toBe( '02:00' );//midway
		expect( d.saveButton().disabled ).toBe( false );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {name: 'Night', days: 63, start: 960, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
		expect( d.close ).toHaveBeenCalledWith( true );
	} );
	it( "fills an edit from the shift, and has nothing to save until it changes", async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day} );
		await d.settle();
		expect( d.root.querySelector( 'h2' )?.textContent ).toBe( 'Edit Day' );
		expect( d.inputs( 'input[type=time]' ).map( i=>i.value ) ).toEqual( ['06:00', '14:00', '10:00'] );
		expect( d.inputs( 'input[type=number]' ).map( i=>i.value ) ).toEqual( ['30'] );
		expect( d.saveButton().disabled ).toBe( true );
		d.dialog.removeBreak( 0 );
		await d.settle();
		expect( d.saveButton().disabled ).toBe( false );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {name: 'Day', days: 31, start: 0, minutes: 480, breaks: null} );
	} );
	it( 'says what is wrong with the form once it is edited, and saves nothing', async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day} );
		await d.settle();
		await d.type( d.inputs( 'input[type=time]' )[2], '15:00' );
		expect( d.root.querySelector( '.problem' )?.textContent ).toBe( 'The break at 15:00 is outside the shift.' );
		expect( d.saveButton().disabled ).toBe( true );
		await d.dialog.save();
		expect( d.save ).not.toHaveBeenCalled();
	} );
	//a refusal shows as the form's error, and the dialog stays for the fix
	it( "shows the server's refusal on the form and stays open", async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day}, vi.fn( async ()=>{ throw new Error( "Shifts 'Day' on Monday and 'Night' on Monday overlap." ); } ) );
		await d.settle();
		await d.type( d.inputs( 'input[type=time]' )[1], '23:00' );
		d.saveButton().click();
		await d.settle();
		expect( d.root.querySelector( '.refused' )?.textContent ).toContain( "Shifts 'Day' on Monday and 'Night' on Monday overlap." );
		expect( d.close ).not.toHaveBeenCalled();
		expect( d.dialog.busy() ).toBe( false );
	} );
} );

describe( 'CalendarExceptionDialog', ()=>{
	it( 'opens an exception over whole days, and saves the days as their range with no zone', async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday} );
		await d.settle();
		expect( d.root.querySelector( 'h2' )?.textContent ).toBe( 'Edit Independence Day' );
		expect( d.inputs( 'input[type=date]' ).map( i=>i.value ) ).toEqual( ['2026-07-04', '2026-07-04'] );
		expect( d.root.querySelector( '.hint' )?.textContent ).toBe( '2026-07-04 06:00 to 2026-07-05 06:00, America/New_York' );
		expect( d.saveButton().disabled ).toBe( true );//nothing changed
		await d.type( d.inputs( 'input[type=date]' )[1], '2026-07-05' );
		expect( d.root.querySelector( '.hint' )?.textContent ).toBe( '2026-07-04 06:00 to 2026-07-06 06:00, America/New_York' );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {name: 'Independence Day', start: '2026-07-04T06:00:00', finish: '2026-07-06T06:00:00', isOpen: false} );
		expect( d.close ).toHaveBeenCalledWith( true );
	} );
	it( 'carries the range between whole days and times', async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday} );
		await d.settle();
		d.dialog.setWhole( false );
		await d.settle();
		expect( d.inputs( 'input[type=datetime-local]' ).map( i=>i.value ) ).toEqual( ['2026-07-04T06:00', '2026-07-05T06:00'] );
		await d.type( d.inputs( 'input[type=datetime-local]' )[1], '2026-07-04T18:00' );//a half day
		expect( d.dialog.model().finish ).toBe( '2026-07-04T18:00' );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {name: 'Independence Day', start: '2026-07-04T06:00:00', finish: '2026-07-04T18:00:00', isOpen: false} );
		d.dialog.setWhole( true );//back:  the days the times touch
		expect( [d.dialog.model().from, d.dialog.model().to] ).toEqual( ['2026-07-04', '2026-07-04'] );
	} );
	it( "opens a new exception closed, on today's production day", async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360} );
		await d.settle();
		expect( d.dialog.model() ).toMatchObject( {name: '', whole: true, isOpen: false} );
		expect( d.dialog.model().from ).toBe( d.dialog.model().to );
		expect( d.root.querySelector( '.hint' )?.textContent ).toContain( '06:00 to ' );//the range shows before the name is typed
		expect( d.saveButton().disabled ).toBe( true );//no name
		await d.type( d.inputs( 'input:not([type])' )[0], 'Extra Saturday' );
		d.dialog.model.update( m=>({...m, isOpen: true}) );
		await d.settle();
		d.saveButton().click();
		await d.settle();
		expect( d.save.mock.calls[0][0] ).toMatchObject( {name: 'Extra Saturday', isOpen: true} );
	} );
	it( "says a range that runs backwards, and the server's refusal, on the form", async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday}, vi.fn( async ()=>{ throw new Error( "(403)Forbidden" ); } ) );
		await d.settle();
		await d.type( d.inputs( 'input[type=date]' )[1], '2026-07-03' );
		expect( d.root.querySelector( '.problem' )?.textContent ).toBe( 'The last day is before the first.' );
		expect( d.saveButton().disabled ).toBe( true );
		await d.type( d.inputs( 'input[type=date]' )[1], '2026-07-06' );
		d.saveButton().click();
		await d.settle();
		expect( d.root.querySelector( '.refused' )?.textContent ).toContain( '(403)Forbidden' );
		expect( d.close ).not.toHaveBeenCalled();
	} );
} );