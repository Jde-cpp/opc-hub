import { CalendarService } from './calendar-service';

//the service over a stub application server:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });

describe( 'CalendarService', ()=>{
	const service = new CalendarService();
	//spec *Reads*:  four flat reads, `deleted: null` on the three tables that have it
	it( 'asks for the live calendars', async ()=>{
		const app = stub( {calendars: [{id: 3, name: 'Plant', slug: 'plant', timeZone: 'America/New_York', dayStart: 360}]} );
		expect( await service.calendars( app ) ).toEqual( [{id: 3, name: 'Plant', slug: 'plant', timeZone: 'America/New_York', dayStart: 360}] );
		expect( app.query ).toHaveBeenCalledWith( 'calendars( deleted: null ){ id name slug timeZone dayStart }' );
		expect( await service.calendars( stub( null ) ) ).toEqual( [] );
	} );
	it( "asks for a calendar's live patterns and exceptions, or every one with its deleted", async ()=>{
		const patterns = stub( {calendarPatterns: [{id: 4, calendarId: 3, effective: null, cycleDays: 7, cycleStart: null}, {id: 5, calendarId: 3, effective: '2026-07-06T10:00:00Z', cycleDays: 14, cycleStart: '2026-07-01T10:00:00Z'}]} );
		const rows = await service.patterns( patterns, 3 );
		expect( rows.map( p=>[p.id, p.effective?.toISOString(), p.cycleDays] ) ).toEqual( [[4, undefined, 7], [5, '2026-07-06T10:00:00.000Z', 14]] );
		expect( patterns.query ).toHaveBeenCalledWith( 'calendarPatterns( calendarId: $calendarId, deleted: null ){ id calendarId effective cycleDays cycleStart }', {calendarId: 3} );
		await service.patterns( patterns, 3, true );
		expect( patterns.query ).toHaveBeenLastCalledWith( 'calendarPatterns( calendarId: $calendarId ){ id calendarId effective cycleDays cycleStart deleted }', {calendarId: 3} );

		const exceptions = stub( {calendarExceptions: [{id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-04T10:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false, deleted: '2026-07-01T00:00:00Z'}]} );
		const [holiday] = await service.exceptions( exceptions, 3, true );
		expect( [holiday.name, holiday.start.toISOString(), holiday.isOpen, holiday.deleted?.toISOString()] ).toEqual( ['Independence Day', '2026-07-04T10:00:00.000Z', false, '2026-07-01T00:00:00.000Z'] );
		expect( exceptions.query ).toHaveBeenCalledWith( 'calendarExceptions( calendarId: $calendarId ){ id calendarId name start finish isOpen deleted }', {calendarId: 3} );
		await service.exceptions( exceptions, 3 );
		expect( exceptions.query ).toHaveBeenLastCalledWith( 'calendarExceptions( calendarId: $calendarId, deleted: null ){ id calendarId name start finish isOpen }', {calendarId: 3} );
	} );
	//a shift has no `deleted`, so its read has no filter
	it( "asks for a pattern's shifts, with their breaks read", async ()=>{
		const app = stub( {calendarShifts: [{id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'}]} );
		expect( await service.shifts( app, 4 ) ).toEqual( [{id: 7, patternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: [{start: 240, minutes: 30}]}] );
		expect( app.query ).toHaveBeenCalledWith( 'calendarShifts( calendarPatternId: $patternId ){ id calendarPatternId name days start minutes breaks }', {patternId: 4} );
	} );
	it( "reads the server's zone, a new calendar's default", async ()=>{
		const app = stub( {status: {timeZone: 'America/New_York'}} );
		expect( await service.serverTimeZone( app ) ).toBe( 'America/New_York' );
		expect( app.query ).toHaveBeenCalledWith( 'status{ timeZone }' );
		expect( await service.serverTimeZone( stub( {status: {}} ) ) ).toBeUndefined();//a server older than the field
	} );
	it( 'spells a mutation with a variable for each arg', ()=>{
		expect( CalendarService.mutation( 'createCalendarShift', {calendarPatternId: 4, name: 'Day'}, '{ id }' ) ).toEqual( {ql: 'mutation createCalendarShift( calendarPatternId: $calendarPatternId, name: $name ){ id }', vars: {calendarPatternId: 4, name: 'Day'}} );
	} );
	it( 'creates, updates and purges a shift', async ()=>{
		const app = stub( {} );
		await service.createShift( app, 4, {name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation createCalendarShift( calendarPatternId: $calendarPatternId, name: $name, days: $days, start: $start, minutes: $minutes, breaks: $breaks ){ id }',
			{calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
		await service.updateShift( app, 7, {minutes: 450, breaks: null} );//null clears the breaks
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation updateCalendarShift( id: $id, minutes: $minutes, breaks: $breaks )', {id: 7, minutes: 450, breaks: null} );
		await service.purgeShift( app, 7 );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation purgeCalendarShift( id: $id )', {id: 7} );
	} );
	//the bounds go as local date-times with no zone, which the server reads in the calendar's
	it( 'creates and updates an exception with zone-less times', async ()=>{
		const app = stub( {} );
		await service.createException( app, 3, {name: 'Independence Day', start: '2026-07-04T06:00:00', finish: '2026-07-05T06:00:00', isOpen: false} );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation createCalendarException( calendarId: $calendarId, name: $name, start: $start, finish: $finish, isOpen: $isOpen ){ id }',
			{calendarId: 3, name: 'Independence Day', start: '2026-07-04T06:00:00', finish: '2026-07-05T06:00:00', isOpen: false} );
		await service.updateException( app, 9, {finish: '2026-07-06T06:00:00'} );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation updateCalendarException( id: $id, finish: $finish )', {id: 9, finish: '2026-07-06T06:00:00'} );
	} );
	it( 'deletes, restores and purges a pattern and an exception by id', async ()=>{
		const app = stub( {} );
		await service.remove( app, 'CalendarPattern', 5, 'delete' );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation deleteCalendarPattern( id: $id )', {id: 5} );
		await service.remove( app, 'CalendarPattern', 5, 'restore' );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation restoreCalendarPattern( id: $id )', {id: 5} );
		await service.remove( app, 'CalendarException', 9, 'purge' );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation purgeCalendarException( id: $id )', {id: 9} );
	} );
	//#397:  the calendar's own two operations, and a pattern's cycle
	it( 'asks for the occurrences of a range, its bounds with no zone', async ()=>{
		const app = stub( {calendarOccurrences: [
			{shift: 7, name: 'Day', day: '2026-07-03', start: '2026-07-03T10:00:00Z', end: '2026-07-03T12:00:00Z', breaks: []},
			{shift: null, name: 'Extra Saturday', day: '2026-07-04', start: '2026-07-04T10:00:00Z', end: '2026-07-04T18:00:00Z', breaks: []}
		]} );
		const [day, extra] = await service.occurrences( app, 3, '2026-06-29T06:00:00', '2026-07-06T06:00:00' );
		expect( [day.shift, day.name, day.day, day.start.toISOString(), day.end.toISOString()] ).toEqual( [7, 'Day', '2026-07-03', '2026-07-03T10:00:00.000Z', '2026-07-03T12:00:00.000Z'] );
		expect( [extra.shift, extra.name] ).toEqual( [undefined, 'Extra Saturday'] );
		expect( app.query ).toHaveBeenCalledWith( 'calendarOccurrences( id: $id, start: $start, end: $end ){ shift name day start end breaks{ start end } }', {id: 3, start: '2026-06-29T06:00:00', end: '2026-07-06T06:00:00'} );
		expect( await service.occurrences( stub( null ), 3, '2026-06-29T06:00:00', '2026-07-06T06:00:00' ) ).toEqual( [] );
	} );
	it( 'copies a pattern to a date and answers the copy', async ()=>{
		const app = stub( {copyCalendarPattern: {id: 6, calendarId: 3, effective: '2026-08-03T10:00:00Z', cycleDays: 7, cycleStart: null}} );
		const copy = await service.copyPattern( app, 4, '2026-08-03T06:00:00' );
		expect( [copy.id, copy.calendarId, copy.effective?.toISOString(), copy.cycleDays, copy.cycleStart] ).toEqual( [6, 3, '2026-08-03T10:00:00.000Z', 7, undefined] );
		expect( app.postQL ).toHaveBeenCalledWith( 'mutation copyCalendarPattern( id: $id, effective: $effective ){ id calendarId effective cycleDays cycleStart }', {id: 4, effective: '2026-08-03T06:00:00'} );
		await expect( service.copyPattern( stub( {} ), 4, '2026-08-03T06:00:00' ) ).rejects.toThrow( 'no copy' );
	} );
	it( "updates a pattern's cycle, a null anchor the week's", async ()=>{
		const app = stub( {} );
		await service.updatePattern( app, 5, {cycleDays: 14, cycleStart: '2026-07-01T06:00:00'} );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation updateCalendarPattern( id: $id, cycleDays: $cycleDays, cycleStart: $cycleStart )', {id: 5, cycleDays: 14, cycleStart: '2026-07-01T06:00:00'} );
		await service.updatePattern( app, 5, {cycleDays: 7, cycleStart: null} );
		expect( app.postQL ).toHaveBeenLastCalledWith( 'mutation updateCalendarPattern( id: $id, cycleDays: $cycleDays, cycleStart: $cycleStart )', {id: 5, cycleDays: 7, cycleStart: null} );
	} );
	it( "hands back the server's refusal", async ()=>{
		const app = { postQL: vi.fn( async ()=>{ throw new Error( "Shifts 'Day' on Monday and 'Overlap' on Monday overlap." ); } ) };
		await expect( service.createShift( app, 4, {name: 'Overlap', days: 31, start: 470, minutes: 480, breaks: null} ) ).rejects.toThrow( 'overlap' );
	} );
} );