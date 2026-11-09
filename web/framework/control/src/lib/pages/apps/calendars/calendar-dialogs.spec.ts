import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { toCalendarException, toCalendarPattern, toCalendarShift } from '../../../model/calendar';
import { CalendarChangeDialog, CalendarChangeDialogData } from './calendar-change-dialog';
import { CalendarCycleDialog, CalendarCycleDialogData } from './calendar-cycle-dialog';
import { CalendarExceptionDialog, CalendarExceptionDialogData } from './calendar-exception-dialog';
import { CalendarShiftDialog, CalendarShiftDialogData } from './calendar-shift-dialog';
import { flush } from 'jde-spa/testing';

const NewYork = 'America/New_York';
const weekly = { cycleDays: 7 };
const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
const holiday = toCalendarException( {id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-04T10:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false} );

//a form over its data, with `save` recorded and the ref's close;  `type` sets a control as a user typing in it does
const open = <T extends {save:unknown}>( component:new()=>any, data:Omit<T,'save'>, save=vi.fn( async ( _args:unknown ):Promise<unknown>=>({}) ) )=>{
	const close = vi.fn( ( _y:unknown )=>{} ), ref = { disableClose: <boolean|undefined>undefined, close };
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
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
	return { fixture, dialog: fixture.componentInstance, root, save, close, ref, settle, type, saveButton, inputs: ( selector='input' )=><HTMLInputElement[]>[...root.querySelectorAll( selector )] };
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
	//#397:  the grid's drag gives the hours and the day
	it( 'opens a new shift on the hours and the day dragged', async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, startAt: 240, minutes: 495, days: [2]} );
		await d.settle();
		expect( d.dialog.model() ).toEqual( {name: '', days: [false, false, true, false, false, false, false], start: '10:00', end: '18:15', breaks: []} );
		expect( d.root.querySelector( '.remove' ) ).toBeNull();//nothing to remove yet
	} );
	it( 'offers Remove on an edit, which closes the form for the page to confirm', async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day} );
		await d.settle();
		d.root.querySelector<HTMLButtonElement>( '.remove' )!.click();
		expect( d.close ).toHaveBeenCalledWith( 'remove' );
		expect( d.save ).not.toHaveBeenCalled();
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
	it( 'says why Save is off once a break is added', async ()=>{
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day} );
		await d.settle();
		d.dialog.addBreak();//midway, on the one already there
		await d.settle();
		expect( d.saveButton().disabled ).toBe( true );
		expect( d.root.querySelector( '.problem' )?.textContent ).toBe( 'The breaks at 10:00 and 10:00 overlap.' );
	} );
	it( 'holds the dialog open while a save is out', async ()=>{
		let answer!:( y:unknown )=>void;
		const d = open<CalendarShiftDialogData>( CalendarShiftDialog, {pattern: weekly, dayStart: 360, shift: day}, vi.fn( ()=>new Promise<unknown>( resolve=>answer = resolve ) ) );
		await d.settle();
		const saving = d.dialog.save();
		expect( d.ref.disableClose ).toBe( true );
		answer( {} );
		await saving;
		expect( d.ref.disableClose ).toBe( false );
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
	it( 'keeps the times when Whole days is unticked with a day blank', async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday} );
		await d.settle();
		d.dialog.model.update( m=>({...m, to: ''}) );
		d.dialog.setWhole( false );
		expect( d.dialog.model() ).toMatchObject( {whole: false, start: '2026-07-04T06:00', finish: '2026-07-05T06:00'} );
	} );
	it( 'takes the times to the production days they are in', async ()=>{
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday} );
		await d.settle();
		d.dialog.setWhole( false );
		d.dialog.model.update( m=>({...m, start: '2026-07-04T02:00', finish: '2026-07-04T05:00'}) );
		d.dialog.setWhole( true );
		expect( [d.dialog.model().from, d.dialog.model().to] ).toEqual( ['2026-07-03', '2026-07-03'] );
	} );
	it( 'holds the dialog open while a save is out', async ()=>{
		let answer!:( y:unknown )=>void;
		const d = open<CalendarExceptionDialogData>( CalendarExceptionDialog, {zone: NewYork, dayStart: 360, exception: holiday}, vi.fn( ()=>new Promise<unknown>( resolve=>answer = resolve ) ) );
		await d.settle();
		const saving = d.dialog.save();
		expect( d.ref.disableClose ).toBe( true );
		answer( {} );
		await saving;
		expect( d.ref.disableClose ).toBe( false );
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

//#397:  Change from… takes a date, and its save copies the pattern
describe( 'CalendarChangeDialog', ()=>{
	const copy = toCalendarPattern( {id: 11, calendarId: 3, effective: '2026-08-03T10:00:00Z', cycleDays: 7, cycleStart: null} );
	const data = { source: '2026-07-06', from: '2026-08-01', zone: NewYork };
	it( 'takes the day the change is in effect from, and closes with the copy', async ()=>{
		const d = open<CalendarChangeDialogData>( CalendarChangeDialog, data, vi.fn( async ()=>copy ) );
		await d.settle();
		expect( d.root.querySelector( '.hint' )?.textContent ).toContain( 'the pattern from 2026-07-06' );
		const [date] = d.inputs( 'input[type=date]' );
		expect( date.value ).toBe( '2026-08-01' );
		await d.type( date, '2026-08-03' );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( '2026-08-03' );
		expect( d.close ).toHaveBeenCalledWith( copy );
	} );
	it( "has nothing to copy to without a day, and shows the server's refusal with the form still open", async ()=>{
		const d = open<CalendarChangeDialogData>( CalendarChangeDialog, data, vi.fn( async ()=>{ throw new Error( "Calendar 'Plant' already has a pattern effective 2026-08-03." ); } ) );
		await d.settle();
		const [date] = d.inputs( 'input[type=date]' );
		await d.type( date, '' );
		expect( d.saveButton().disabled ).toBe( true );
		expect( d.root.querySelector( '.problem' )?.textContent ).toBe( 'Pick the day the change takes effect.' );
		await d.type( date, '2026-08-03' );
		d.saveButton().click();
		await d.settle();
		expect( d.root.querySelector( '.refused' )?.textContent ).toContain( 'already has a pattern effective 2026-08-03' );
		expect( d.close ).not.toHaveBeenCalled();
		expect( [d.dialog.busy(), d.ref.disableClose] ).toEqual( [false, false] );
	} );
} );

describe( 'CalendarCycleDialog', ()=>{
	const rotation = { cycleDays: 14, cycleStart: new Date( '2026-07-01T10:00:00Z' ) };
	it( 'makes a weekly pattern a rotation from the day offered', async ()=>{
		const d = open<CalendarCycleDialogData>( CalendarCycleDialog, {pattern: weekly, zone: NewYork, dayStart: 360, anchor: '2026-07-20'} );
		await d.settle();
		expect( d.dialog.model() ).toEqual( {rotation: false, days: 7, anchor: '2026-07-20'} );
		expect( d.inputs( 'input' ) ).toEqual( [] );//the week has neither a length nor an anchor to enter
		expect( d.saveButton().disabled ).toBe( true );//nothing changed
		d.dialog.model.update( m=>({...m, rotation: true}) );
		await d.settle();
		const [days] = d.inputs( 'input[type=number]' );
		expect( [days.value, d.inputs( 'input[type=date]' )[0].value] ).toEqual( ['7', '2026-07-20'] );
		await d.type( days, '14' );
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {cycleDays: 14, cycleStart: '2026-07-20T06:00:00'} );
		expect( d.close ).toHaveBeenCalledWith( true );
	} );
	it( "fills a rotation's length and anchor, and takes it back to the week", async ()=>{
		const d = open<CalendarCycleDialogData>( CalendarCycleDialog, {pattern: rotation, zone: NewYork, dayStart: 360, anchor: '2026-07-20'} );
		await d.settle();
		expect( [d.inputs( 'input[type=number]' )[0].value, d.inputs( 'input[type=date]' )[0].value] ).toEqual( ['14', '2026-07-01'] );//its own anchor, not the one offered
		expect( d.saveButton().disabled ).toBe( true );
		d.dialog.model.update( m=>({...m, rotation: false}) );
		await d.settle();
		d.saveButton().click();
		await d.settle();
		expect( d.save ).toHaveBeenCalledWith( {cycleDays: 7, cycleStart: null} );
	} );
	it( "says a length that is no rotation, and the server's refusal, on the form", async ()=>{
		const d = open<CalendarCycleDialogData>( CalendarCycleDialog, {pattern: rotation, zone: NewYork, dayStart: 360, anchor: '2026-07-20'}, vi.fn( async ()=>{ throw new Error( "Shift 'Day' runs on day 9, past a 8-day cycle." ); } ) );
		await d.settle();
		const [days] = d.inputs( 'input[type=number]' );
		await d.type( days, '40' );
		expect( d.root.querySelector( '.problem' )?.textContent ).toBe( 'A rotation is 1 to 31 days long.' );
		expect( d.saveButton().disabled ).toBe( true );
		await d.type( days, '8' );
		d.saveButton().click();
		await d.settle();
		expect( d.root.querySelector( '.refused' )?.textContent ).toContain( 'past a 8-day cycle' );
		expect( d.close ).not.toHaveBeenCalled();
	} );
} );