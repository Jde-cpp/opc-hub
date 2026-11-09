import { addDays, breaksText, Calendar, changedArgs, cycleArgsOf, cycleDayNames, cycleDayOf, cycleProblem, daysDetail, daysMask, dayStartInstant, drawnSpan, durationText, exceptionArgsOf, exceptionBands, exceptionProblem, exceptionRange, gridHours, gridPieces, isWeekly, maskDays, minutesToPixels, minutesToTime, mondayOf, occurrenceBars, openTone, patternBars, patternColumns, patternDate, patternInEffect, pixelsToMinutes, productionDay, productionDays, shiftArgsOf, ShiftForm, shiftMinutes, shiftProblem, timeToMinutes, toCalendarException, toCalendarOccurrence, toCalendarPattern, toCalendarShift, toCycleArgs, toCycleForm, toExceptionArgs, toExceptionForm, toShiftArgs, toShiftForm, weekColumns, weekdayOf, weekRange, wholeDayRange, wholeDays, zoneInstant, zoneLess, zoneLocal } from './calendar';

//America/New_York, as the server's suites:  EDT is UTC-4 from 2026-03-08 to 2026-11-01, so a 06:00 day start is 10:00Z in July.
const NewYork = 'America/New_York';
const weekly = { cycleDays: 7 }, rotation = { cycleDays: 14, cycleStart: new Date( '2026-07-01T10:00:00Z' ) };
const utc = ( text:string )=>new Date( `${text}Z` );

describe( 'Calendar', ()=>{
	//the day-start conversion:  minutes after midnight in the column, a time of day in the form
	it( 'reads the day start as a time of day, and a time of day as minutes', ()=>{
		expect( minutesToTime( 360 ) ).toBe( '06:00' );
		expect( minutesToTime( 0 ) ).toBe( '00:00' );
		expect( minutesToTime( 1439 ) ).toBe( '23:59' );
		expect( minutesToTime( 1440+90 ) ).toBe( '01:30' );//past midnight:  a shift's end
		expect( timeToMinutes( '06:00' ) ).toBe( 360 );
		expect( timeToMinutes( '23:59:30' ) ).toBe( 1439 );//an input with seconds
		expect( timeToMinutes( '' ) ).toBeUndefined();
		expect( timeToMinutes( '24:00' ) ).toBeUndefined();
		expect( timeToMinutes( '6' ) ).toBeUndefined();
	} );
	it( 'holds a row with its zone and day start, midnight on a new one', ()=>{
		const row = new Calendar( {id: 3, name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 360} );
		expect( [row.type, row.timeZone, row.dayStart, row.canSave] ).toEqual( ['Calendar', NewYork, 360, true] );
		const blank = new Calendar( {} );
		expect( [blank.timeZone, blank.dayStart, blank.canSave] ).toEqual( [undefined, 0, false] );
		expect( new Calendar( {name: 'Plant', slug: 'plant'} ).canSave ).toBe( false );//no zone
	} );
	//a blank time is no day start, kept across the copy every edit makes, and nothing to save
	it( "can't save without a day start", ()=>{
		const edited = new Calendar( {name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: null} );
		expect( edited.dayStart ).toBeNull();
		expect( edited.properties.dayStart ).toBeNull();
		expect( edited.canSave ).toBe( false );
		expect( new Calendar( {name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 1440} ).canSave ).toBe( false );
	} );
	it( 'creates with its zone and day start, and updates what changed', ()=>{
		const created = new Calendar( {name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 360} ).mutation( new Calendar( {} ) );
		expect( created.map( m=>m.toString() ) ).toEqual( ['createCalendar( slug:"plant", name:"Plant", timeZone:"America/New_York", dayStart:360 )'] );
		const row = new Calendar( {id: 3, name: 'Plant', slug: 'plant', timeZone: NewYork, dayStart: 360} );
		expect( new Calendar( {...row, dayStart: 420} ).mutation( row ).map( m=>m.toString() ) ).toEqual( ['updateCalendar( id:3, dayStart:420 )'] );
		expect( new Calendar( {...row, timeZone: 'Europe/Berlin'} ).mutation( row ).map( m=>m.toString() ) ).toEqual( ['updateCalendar( id:3, timeZone:"Europe/Berlin" )'] );
		expect( row.properties.mutation( row ) ).toEqual( [] );
		expect( row.equals( row.properties ) ).toBe( true );
		expect( row.equals( new Calendar( {...row, dayStart: 0} ) ) ).toBe( false );
		expect( new Calendar( {name: 'Plant', slug: 'plant', timeZone: NewYork} ).mutation( new Calendar( {} ) ).map( m=>m.toString() ) ).toEqual( ['createCalendar( slug:"plant", name:"Plant", timeZone:"America/New_York" )'] );//midnight is the column's default
	} );
} );

describe( 'calendar rows', ()=>{
	it( 'maps a pattern, a shift and an exception', ()=>{
		expect( toCalendarPattern( {id: 4, calendarId: 3, effective: null, cycleDays: 7, cycleStart: null} ) ).toEqual( {id: 4, calendarId: 3, effective: undefined, cycleDays: 7, cycleStart: undefined, deleted: undefined} );
		const second = toCalendarPattern( {id: 5, calendarId: 3, effective: '2026-07-06T10:00:00Z', cycleDays: 14, cycleStart: '2026-07-01T10:00:00Z', deleted: '2026-07-07T00:00:00Z'} );
		expect( [second.effective, second.cycleStart, second.deleted] ).toEqual( [utc( '2026-07-06T10:00:00' ), utc( '2026-07-01T10:00:00' ), utc( '2026-07-07T00:00:00' )] );
		expect( toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} ) ).toEqual( {id: 7, patternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: [{start: 240, minutes: 30}]} );
		expect( toCalendarShift( {id: 8, calendarPatternId: 4, name: 'Night', days: 31, start: 960, minutes: 480, breaks: null} ).breaks ).toEqual( [] );
		expect( toCalendarShift( {id: 9, calendarPatternId: 4, name: 'Odd', days: 1, start: 0, minutes: 60, breaks: 'garbage'} ).breaks ).toEqual( [] );
		expect( toCalendarException( {id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-03T22:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false} ) )
			.toEqual( {id: 9, calendarId: 3, name: 'Independence Day', start: utc( '2026-07-03T22:00:00' ), finish: utc( '2026-07-05T10:00:00' ), isOpen: false, deleted: undefined} );
	} );
} );

describe( "a calendar's wall clock", ()=>{
	//the zone-less formatting of a local date-time and its parse back
	it( 'shows an instant as a local date-time in the zone, with no zone in it', ()=>{
		expect( zoneLocal( utc( '2026-07-03T22:00:00' ), NewYork ) ).toBe( '2026-07-03T18:00' );
		expect( zoneLocal( utc( '2026-01-15T11:00:00' ), NewYork ) ).toBe( '2026-01-15T06:00' );//EST
		expect( zoneLocal( utc( '2026-07-04T03:30:00' ), NewYork ) ).toBe( '2026-07-03T23:30' );//the day before
		expect( zoneLocal( utc( '2026-07-03T22:00:15' ), NewYork ) ).toBe( '2026-07-03T18:00:15' );//its seconds, where it has them
		expect( zoneLocal( utc( '2026-07-03T22:00:00' ), 'UTC' ) ).toBe( '2026-07-03T22:00' );
		expect( zoneLocal( utc( '2026-07-03T22:00:00' ), 'Asia/Kolkata' ) ).toBe( '2026-07-04T03:30' );
		expect( zoneLess( '2026-07-03T18:00' ) ).toBe( '2026-07-03T18:00:00' );//as a mutation sends it
		expect( zoneLess( '2026-07-03T18:00:15' ) ).toBe( '2026-07-03T18:00:15' );
	} );
	it( 'reads a local date-time back as its instant', ()=>{
		expect( zoneInstant( '2026-07-03T18:00', NewYork ) ).toEqual( utc( '2026-07-03T22:00:00' ) );
		expect( zoneInstant( '2026-01-15T06:00:00', NewYork ) ).toEqual( utc( '2026-01-15T11:00:00' ) );
		expect( zoneInstant( '2026-07-04T03:30', 'Asia/Kolkata' ) ).toEqual( utc( '2026-07-03T22:00:00' ) );
		for( const text of ['2026-07-03T18:00', '2026-03-08T06:00', '2026-11-01T06:00', '2026-12-31T23:59'] )
			expect( zoneLocal( zoneInstant( text, NewYork )!, NewYork ) ).toBe( text );
		expect( zoneInstant( '2026-07-03', NewYork ) ).toBeUndefined();
		expect( zoneInstant( 'garbage', NewYork ) ).toBeUndefined();
	} );
	//as the server's choose::earliest:  the first pass of an hour the clocks repeat, and the jump for one they skip
	it( 'takes the first of a repeated time, and the jump for a skipped one', ()=>{
		expect( zoneInstant( '2026-11-01T01:30', NewYork ) ).toEqual( utc( '2026-11-01T05:30:00' ) );//EDT's, not EST's 06:30Z
		expect( zoneInstant( '2026-03-08T02:30', NewYork ) ).toEqual( utc( '2026-03-08T07:00:00' ) );//02:00 EST became 03:00 EDT
		expect( zoneInstant( '2026-03-08T01:59', NewYork ) ).toEqual( utc( '2026-03-08T06:59:00' ) );
		expect( zoneInstant( '2026-03-08T03:00', NewYork ) ).toEqual( utc( '2026-03-08T07:00:00' ) );
	} );
	it( 'steps a date by days', ()=>{
		expect( addDays( '2026-02-28', 1 ) ).toBe( '2026-03-01' );
		expect( addDays( '2026-01-01', -1 ) ).toBe( '2025-12-31' );
	} );
} );

describe( "a pattern's cycle", ()=>{
	it( 'is the week unless it is a rotation', ()=>{
		expect( isWeekly( weekly ) ).toBe( true );
		expect( isWeekly( rotation ) ).toBe( false );
		expect( isWeekly( {cycleDays: 7, cycleStart: new Date()} ) ).toBe( false );//a week anchored on a date
		expect( cycleDayNames( weekly )[0] ).toBe( 'Monday' );
		expect( cycleDayNames( rotation ) ).toHaveLength( 14 );
		expect( cycleDayNames( rotation )[13] ).toBe( 'Day 14' );
	} );
	it( 'reads the mask to days and back', ()=>{
		expect( maskDays( 31, 7 ) ).toEqual( [0, 1, 2, 3, 4] );
		expect( maskDays( 96, 7 ) ).toEqual( [5, 6] );
		expect( daysMask( [0, 1, 2, 3, 4] ) ).toBe( 31 );
		expect( daysMask( [6, 6] ) ).toBe( 64 );
		expect( daysMask( maskDays( 2**30+1, 31 ) ) ).toBe( 2**30+1 );//a 31-day cycle's last day
		expect( maskDays( 255, 7 ) ).toEqual( [0, 1, 2, 3, 4, 5, 6] );//a bit beyond the cycle is not a day
	} );
	it( "says a shift's days by the week's names, or a rotation's numbers", ()=>{
		expect( daysDetail( 96, weekly ) ).toBe( 'Saturday, Sunday' );
		expect( daysDetail( 0b101, rotation ) ).toBe( 'Day 1, Day 3' );
		expect( daysDetail( 0, weekly ) ).toBe( 'No days' );
	} );
	it( 'picks the pattern in effect at an instant', ()=>{
		const first = toCalendarPattern( {id: 4, calendarId: 3, effective: null} ), july = toCalendarPattern( {id: 5, calendarId: 3, effective: '2026-07-06T10:00:00Z'} ), august = toCalendarPattern( {id: 6, calendarId: 3, effective: '2026-08-03T10:00:00Z'} );
		const all = [august, first, july];
		expect( patternInEffect( all, utc( '2026-07-01T00:00:00' ) ) ).toBe( first );
		expect( patternInEffect( all, utc( '2026-07-06T10:00:00' ) ) ).toBe( july );//from its instant
		expect( patternInEffect( all, utc( '2026-07-06T09:59:59' ) ) ).toBe( first );
		expect( patternInEffect( all, utc( '2026-09-01T00:00:00' ) ) ).toBe( august );
		expect( patternInEffect( [first, {...july, deleted: new Date()}], utc( '2026-07-10T00:00:00' ) ) ).toBe( first );//a deleted one is not in effect
		expect( patternInEffect( [{...july, id: 9}, july], utc( '2026-07-10T00:00:00' ) ) ).toBe( july );//the lowest id of a tie
		expect( patternInEffect( [july], utc( '2026-07-01T00:00:00' ) ) ).toBeUndefined();
		expect( patternDate( july, NewYork ) ).toBe( '2026-07-06' );//06:00 local, the day's start
		expect( patternDate( first, NewYork ) ).toBeUndefined();
	} );
} );

describe( 'a shift', ()=>{
	const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
	const night = toCalendarShift( {id: 8, calendarPatternId: 4, name: 'Night', days: 31, start: 960, minutes: 480, breaks: null} );
	it( 'shows its times on the wall clock, from the day start', ()=>{
		expect( breaksText( day, 360 ) ).toBe( '10:00 (30 min)' );
		expect( durationText( 480 ) ).toBe( '8 h' );
		expect( durationText( 510 ) ).toBe( '8 h 30 min' );
		expect( durationText( 45 ) ).toBe( '45 min' );
	} );
	it( 'fills its form with times of day, and makes its columns from one', ()=>{
		const form = toShiftForm( night, weekly, 360 );
		expect( form ).toEqual( {name: 'Night', days: [true, true, true, true, true, false, false], start: '22:00', end: '06:00', breaks: []} );
		expect( shiftMinutes( form ) ).toBe( 480 );//into the next day
		expect( toShiftArgs( form, 360 ) ).toEqual( shiftArgsOf( night ) );
		expect( toShiftArgs( toShiftForm( day, weekly, 360 ), 360 ) ).toEqual( {name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
		//a shift entered before the day start is late in the production day
		expect( toShiftArgs( {name: ' Early ', days: [true], start: '04:00', end: '06:00', breaks: []}, 360 ) ).toEqual( {name: 'Early', days: 1, start: 1320, minutes: 120, breaks: null} );
		expect( shiftMinutes( {start: '06:00', end: '06:00'} ) ).toBe( 1440 );//the same time is the whole day
		expect( shiftMinutes( {start: '06:00', end: '06:01'} ) ).toBe( 1 );
		expect( shiftMinutes( {start: '', end: '06:00'} ) ).toBeUndefined();
	} );
	it( 'orders its breaks from the start, a break past midnight after one before it', ()=>{
		const form:ShiftForm = {name: 'Night', days: [true], start: '22:00', end: '06:00', breaks: [{start: '02:00', minutes: 30}, {start: '23:00', minutes: 15}]};
		expect( shiftProblem( form ) ).toBeUndefined();
		expect( toShiftArgs( form, 360 ).breaks ).toBe( '[{"start":60,"minutes":15},{"start":240,"minutes":30}]' );
	} );
	it( 'says what the server would refuse', ()=>{
		const form:ShiftForm = {name: 'Day', days: [true, false], start: '06:00', end: '14:00', breaks: []};
		expect( shiftProblem( form ) ).toBeUndefined();
		expect( shiftProblem( {...form, name: ' '} ) ).toContain( 'name' );
		expect( shiftProblem( {...form, days: [false, false]} ) ).toContain( 'days' );
		expect( shiftProblem( {...form, end: ''} ) ).toContain( 'start and end' );
		expect( shiftProblem( {...form, breaks: [{start: '13:45', minutes: 30}]} ) ).toBe( 'The break at 13:45 is outside the shift.' );
		expect( shiftProblem( {...form, breaks: [{start: '05:00', minutes: 30}]} ) ).toContain( 'outside the shift' );
		expect( shiftProblem( {...form, breaks: [{start: '10:00', minutes: 0}]} ) ).toContain( 'A break needs' );
		expect( shiftProblem( {...form, breaks: [{start: '10:00', minutes: <number><unknown>null}]} ) ).toContain( 'A break needs' );//a number input emptied
		expect( shiftProblem( {...form, breaks: [{start: '10:15', minutes: 30}, {start: '10:00', minutes: 30}]} ) ).toBe( 'The breaks at 10:00 and 10:15 overlap.' );
		expect( shiftProblem( {...form, breaks: [{start: '10:00', minutes: 30}, {start: '10:30', minutes: 30}]} ) ).toBeUndefined();//one after the other
	} );
	it( 'sends only what an edit changed', ()=>{
		expect( changedArgs( {...shiftArgsOf( day ), minutes: 450}, shiftArgsOf( day ) ) ).toEqual( {minutes: 450} );
		expect( changedArgs( {...shiftArgsOf( day ), breaks: null}, shiftArgsOf( day ) ) ).toEqual( {breaks: null} );
		expect( changedArgs( shiftArgsOf( day ), shiftArgsOf( day ) ) ).toEqual( {} );
	} );
} );

describe( 'an exception', ()=>{
	const holiday = toCalendarException( {id: 9, calendarId: 3, name: 'Independence Day', start: '2026-07-04T10:00:00Z', finish: '2026-07-05T10:00:00Z', isOpen: false} );
	const halfDay = toCalendarException( {id: 10, calendarId: 3, name: 'Stocktake', start: '2026-07-03T22:00:00Z', finish: '2026-07-04T02:00:00Z', isOpen: true} );
	it( 'covers whole production days, each from the day start to the next', ()=>{
		expect( wholeDayRange( '2026-07-04', '2026-07-04', 360 ) ).toEqual( {start: '2026-07-04T06:00', finish: '2026-07-05T06:00'} );
		expect( wholeDayRange( '2026-12-31', '2027-01-01', 0 ) ).toEqual( {start: '2026-12-31T00:00', finish: '2027-01-02T00:00'} );
		expect( wholeDays( '2026-07-04T06:00', '2026-07-06T06:00', 360 ) ).toEqual( {from: '2026-07-04', to: '2026-07-05'} );
		expect( wholeDays( '2026-07-04T06:00', '2026-07-05T18:00', 360 ) ).toBeUndefined();//ends inside a day
		expect( wholeDays( '2026-07-04T00:00', '2026-07-05T00:00', 360 ) ).toBeUndefined();//midnight is not this calendar's day
		expect( wholeDays( '2026-07-04T06:00', '2026-07-04T06:00', 360 ) ).toBeUndefined();
	} );
	it( 'finds the production days a range touches', ()=>{
		expect( productionDays( '2026-07-04T02:00', '2026-07-04T05:00', 360 ) ).toEqual( {from: '2026-07-03', to: '2026-07-03'} );//before the day start:  the day before's
		expect( productionDays( '2026-07-04T06:00', '2026-07-06T06:00', 360 ) ).toEqual( {from: '2026-07-04', to: '2026-07-05'} );//aligned
		expect( productionDays( '2026-07-04T10:00', '2026-07-05T06:00', 360 ) ).toEqual( {from: '2026-07-04', to: '2026-07-04'} );//an end on a day's start isn't in that day
		expect( productionDays( '2026-07-04T10:00', '2026-07-05T00:00', 0 ) ).toEqual( {from: '2026-07-04', to: '2026-07-04'} );
		expect( productionDays( '2026-07-04T10:00', '', 360 ) ).toEqual( {from: '2026-07-04', to: '2026-07-04'} );//no end yet
	} );
	it( "fills its form on the calendar's wall clock, whole days where its range is", ()=>{
		expect( toExceptionForm( holiday, NewYork, 360 ) ).toEqual( {name: 'Independence Day', whole: true, from: '2026-07-04', to: '2026-07-04', start: '2026-07-04T06:00', finish: '2026-07-05T06:00', isOpen: false} );
		expect( toExceptionForm( halfDay, NewYork, 360 ) ).toEqual( {name: 'Stocktake', whole: false, from: '2026-07-03', to: '2026-07-03', start: '2026-07-03T18:00', finish: '2026-07-03T22:00', isOpen: true} );
		//06:30 EDT on the morning the clocks went forward is that day's, though six hours before it was 23:30 the day before
		expect( toExceptionForm( undefined, NewYork, 360, utc( '2026-03-08T10:30:00' ) ).from ).toBe( '2026-03-08' );
		//a new one:  today's production day, which at 02:00 local is still yesterday's
		expect( toExceptionForm( undefined, NewYork, 360, utc( '2026-07-10T06:00:00' ) ) ).toEqual( {name: '', whole: true, from: '2026-07-09', to: '2026-07-09', start: '2026-07-09T06:00', finish: '2026-07-10T06:00', isOpen: false} );
	} );
	it( 'sends its range with no zone, however it was entered', ()=>{
		const whole = toExceptionForm( holiday, NewYork, 360 );
		expect( toExceptionArgs( {...whole, name: ' Fourth ', to: '2026-07-05'}, 360 ) ).toEqual( {name: 'Fourth', start: '2026-07-04T06:00:00', finish: '2026-07-06T06:00:00', isOpen: false} );
		expect( toExceptionArgs( toExceptionForm( halfDay, NewYork, 360 ), 360 ) ).toEqual( {name: 'Stocktake', start: '2026-07-03T18:00:00', finish: '2026-07-03T22:00:00', isOpen: true} );
		expect( exceptionArgsOf( halfDay, NewYork ) ).toEqual( toExceptionArgs( toExceptionForm( halfDay, NewYork, 360 ), 360 ) );//an edit that changes nothing sends nothing
		expect( exceptionRange( {...whole, whole: false, start: '2026-07-04T08:00'}, 360 ) ).toEqual( {start: '2026-07-04T08:00', finish: '2026-07-05T06:00'} );
	} );
	it( 'says what the server would refuse', ()=>{
		const form = toExceptionForm( holiday, NewYork, 360 );
		expect( exceptionProblem( form, NewYork, 360 ) ).toBeUndefined();
		expect( exceptionProblem( {...form, name: ''}, NewYork, 360 ) ).toContain( 'name' );
		expect( exceptionProblem( {...form, to: '2026-07-03'}, NewYork, 360 ) ).toBe( 'The last day is before the first.' );
		expect( exceptionProblem( {...form, to: ''}, NewYork, 360 ) ).toContain( 'first and last day' );
		expect( exceptionProblem( {...form, from: '2026-02-30'}, NewYork, 360 ) ).toContain( 'first and last day' );
		const timed = {...form, whole: false};
		expect( exceptionProblem( {...timed, finish: timed.start}, NewYork, 360 ) ).toBe( 'The end is not after the start.' );
		expect( exceptionProblem( {...timed, finish: ''}, NewYork, 360 ) ).toContain( 'start and end' );
		//by instants:  the hour the clocks repeat ends where it starts, as the server reads both
		expect( exceptionProblem( {...timed, start: '2026-11-01T01:00', finish: '2026-11-01T01:30'}, NewYork, 360 ) ).toBeUndefined();
	} );
} );

//#397:  the grid's layout, in minutes from a column's top, and what the page and the Preview draw on it
describe( 'the grid', ()=>{
	const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
	const night = toCalendarShift( {id: 8, calendarPatternId: 4, name: 'Night', days: 31, start: 960, minutes: 480, breaks: null} );
	//02:00 to 10:00 under a 06:00 day start:  four hours of it are past the production day's end
	const late = toCalendarShift( {id: 9, calendarPatternId: 4, name: 'Late', days: 65, start: 1200, minutes: 480, breaks: '[{"start":200,"minutes":60}]'} );
	const place = ( p:{column:number; top:number; minutes:number} )=>[p.column, p.top, p.minutes];

	it( 'turns minutes to pixels and a pointer back to the quarter hour', ()=>{
		expect( minutesToPixels( 60, 28 ) ).toBe( 28 );
		expect( minutesToPixels( 90, 28 ) ).toBe( 42 );
		expect( minutesToPixels( 1440, 28 ) ).toBe( 672 );//a column's height
		expect( pixelsToMinutes( 42, 28 ) ).toBe( 90 );
		expect( pixelsToMinutes( 45, 28 ) ).toBe( 90 );//96 minutes down:  the nearer quarter
		expect( pixelsToMinutes( 50, 28 ) ).toBe( 105 );
		expect( pixelsToMinutes( -5, 28 ) ).toBe( 0 );//above the column
		expect( pixelsToMinutes( 9999, 28 ) ).toBe( 1440 );//below it
	} );
	it( "counts the hours down from the day's start", ()=>{
		const hours = gridHours( 360 );
		expect( hours ).toHaveLength( 24 );
		expect( [hours[0], hours[18], hours[23]] ).toEqual( ['06:00', '00:00', '05:00'] );
		expect( gridHours( 390 )[1] ).toBe( '07:30' );//a day start off the hour
	} );
	it( 'reads a drag either way as its hours, and a click as nothing', ()=>{
		expect( drawnSpan( 480, 960 ) ).toEqual( {start: 480, minutes: 480} );
		expect( drawnSpan( 960, 480 ) ).toEqual( {start: 480, minutes: 480} );//dragged upwards
		expect( drawnSpan( 480, 495 ) ).toEqual( {start: 480, minutes: 15} );
		expect( drawnSpan( 480, 480 ) ).toBeUndefined();
	} );

	it( 'finds the weekday and the Monday of a date, and the production day of an instant', ()=>{
		expect( [weekdayOf( '2026-07-20' ), weekdayOf( '2026-07-26' ), weekdayOf( '1969-12-29' )] ).toEqual( [0, 6, 0] );
		expect( mondayOf( '2026-07-23' ) ).toBe( '2026-07-20' );
		expect( mondayOf( '2026-07-20' ) ).toBe( '2026-07-20' );
		expect( dayStartInstant( '2026-07-10', NewYork, 360 ) ).toEqual( utc( '2026-07-10T10:00:00' ) );
		expect( productionDay( utc( '2026-07-10T10:00:00' ), NewYork, 360 ) ).toBe( '2026-07-10' );//from its start
		expect( productionDay( utc( '2026-07-10T06:00:00' ), NewYork, 360 ) ).toBe( '2026-07-09' );//02:00 local is still the day before's
		//the hour the clocks repeat, under a day start inside it:  the day began at the first 01:30, so the second 01:15 is in it
		expect( productionDay( utc( '2026-11-01T06:15:00' ), NewYork, 90 ) ).toBe( '2026-11-01' );
		expect( productionDay( utc( '2026-11-01T05:15:00' ), NewYork, 90 ) ).toBe( '2026-10-31' );//the first 01:15
	} );
	//the columns of a 14-day cycle from an anchor:  Wednesday 2026-07-01 was its day 1
	it( "numbers a rotation's days from its anchor, with the weekday each falls on for the chosen date", ()=>{
		expect( ['2026-07-01', '2026-07-15', '2026-07-20', '2026-06-30'].map( date=>cycleDayOf( rotation, date, NewYork ) ) ).toEqual( [0, 0, 5, 13] );//before the anchor too
		expect( cycleDayOf( rotation, '2026-11-02', NewYork ) ).toBe( 12 );//124 days on, counted in dates across the clock change
		const columns = patternColumns( rotation, '2026-07-20', NewYork );
		expect( columns ).toHaveLength( 14 );
		expect( columns[0] ).toEqual( {title: 'Day 1', short: 'Day 1', sub: 'Wed 07-15', current: false} );//the cycle the 20th is in began on the 15th
		expect( columns[5] ).toEqual( {title: 'Day 6', short: 'Day 6', sub: 'Mon 07-20', current: true} );
		expect( columns[13] ).toEqual( {title: 'Day 14', short: 'Day 14', sub: 'Tue 07-28', current: false} );
		expect( patternColumns( rotation, '2026-06-30', NewYork )[13] ).toEqual( {title: 'Day 14', short: 'Day 14', sub: 'Tue 06-30', current: true} );
		//a week anchored on a date is a rotation of seven
		expect( patternColumns( {cycleDays: 7, cycleStart: rotation.cycleStart}, '2026-07-02', NewYork ).map( c=>c.sub ) ).toEqual( ['Wed 07-01', 'Thu 07-02', 'Fri 07-03', 'Sat 07-04', 'Sun 07-05', 'Mon 07-06', 'Tue 07-07'] );
	} );
	it( "names the week's seven columns, the chosen date's marked", ()=>{
		const columns = patternColumns( weekly, '2026-07-22', NewYork );
		expect( columns.map( c=>c.title ) ).toEqual( ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'] );
		expect( columns.map( c=>c.short ).join( ' ' ) ).toBe( 'Mon Tue Wed Thu Fri Sat Sun' );
		expect( columns.map( c=>c.current ) ).toEqual( [false, false, true, false, false, false, false] );
		expect( columns.every( c=>c.sub===undefined ) ).toBe( true );
		const week = weekColumns( '2026-06-29', '2026-07-04' );
		expect( week.map( c=>c.sub ) ).toEqual( ['2026-06-29', '2026-06-30', '2026-07-01', '2026-07-02', '2026-07-03', '2026-07-04', '2026-07-05'] );
		expect( week.findIndex( c=>c.current ) ).toBe( 5 );
	} );

	//the shift crossing the day's end
	it( "cuts a span where a column ends, and continues it at the top of the next", ()=>{
		expect( gridPieces( {from: 960, to: 1440}, [], 7, true ) ).toEqual( [{column: 0, top: 960, minutes: 480, cuts: [], continued: false, continues: false}] );//to the day's end, not past it
		const pieces = gridPieces( {from: 1200, to: 1680}, [{from: 1400, to: 1460}], 7, true );
		expect( pieces ).toEqual( [
			{column: 0, top: 1200, minutes: 240, cuts: [{top: 1400, minutes: 40}], continued: false, continues: true},
			{column: 1, top: 0, minutes: 240, cuts: [{top: 0, minutes: 20}], continued: true, continues: false}//the break crosses with it
		] );
		//the cycle's last day runs into its first;  a week's last runs off the week
		expect( gridPieces( {from: 6*1440+1200, to: 6*1440+1680}, [], 7, true ).map( place ) ).toEqual( [[6, 1200, 240], [0, 0, 240]] );
		expect( gridPieces( {from: 6*1440+1200, to: 6*1440+1680}, [], 7, false ).map( place ) ).toEqual( [[6, 1200, 240]] );
		expect( gridPieces( {from: -240, to: 240}, [], 7, false ) ).toEqual( [{column: 0, top: 0, minutes: 240, cuts: [], continued: true, continues: false}] );//begun before the week
		expect( gridPieces( {from: 0, to: 3*1440}, [], 7, false ).map( place ) ).toEqual( [[0, 0, 1440], [1, 0, 1440], [2, 0, 1440]] );//an exception over three days
		expect( gridPieces( {from: 1200, to: 1680}, [], 1, true ).map( place ) ).toEqual( [[0, 1200, 240], [0, 0, 240]] );//a one-day cycle
	} );
	it( "lays a pattern's shifts out as a bar on each of its days, the breaks cut out", ()=>{
		const bars = patternBars( [day, night], weekly, 360 );
		expect( bars ).toHaveLength( 10 );
		expect( bars.filter( b=>b.shift==7 ).map( place ) ).toEqual( [[0, 0, 480], [1, 0, 480], [2, 0, 480], [3, 0, 480], [4, 0, 480]] );
		expect( bars[0] ).toMatchObject( {key: '7-0-0', name: 'Day', times: '06:00 - 14:00', cuts: [{top: 240, minutes: 30}], tone: 1, primary: true,
			detail: 'Day, Monday, Tuesday, Wednesday, Thursday, Friday, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)'} );
		expect( bars.filter( b=>b.primary ).map( b=>b.key ) ).toEqual( ['7-0-0', '8-0-0'] );//a shift's first bar
		expect( bars[5] ).toMatchObject( {column: 0, top: 960, minutes: 480, times: '22:00 - 06:00', continues: false, tone: 2, detail: 'Night, Monday, Tuesday, Wednesday, Thursday, Friday, 22:00 to 06:00, 8 h'} );
	} );
	it( "continues a shift past the production day's end in the next day's column, Sunday's in Monday's", ()=>{
		const bars = patternBars( [late], weekly, 360 );
		expect( bars.map( b=>[b.key, ...place( b ), b.continued, b.continues] ) ).toEqual( [
			['9-0-0', 0, 1200, 240, false, true], ['9-0-1', 1, 0, 240, true, false],
			['9-6-0', 6, 1200, 240, false, true], ['9-6-1', 0, 0, 240, true, false]
		] );
		expect( bars.map( b=>b.cuts ) ).toEqual( [[{top: 1400, minutes: 40}], [{top: 0, minutes: 20}], [{top: 1400, minutes: 40}], [{top: 0, minutes: 20}]] );
		expect( bars[0].times ).toBe( '02:00 - 10:00' );
		expect( bars.filter( b=>b.primary ) ).toHaveLength( 1 );
		expect( patternBars( [{...day, days: 0b101}], rotation, 360 ).map( b=>[b.column, b.detail] ) ).toEqual( [[0, 'Day, Day 1, Day 3, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)'], [2, 'Day, Day 1, Day 3, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)']] );
	} );

	//the Preview:  calendarOccurrences' answer for the week of 2026-06-29, EDT, under a 06:00 day start
	const occurrence = ( shift:number|null, name:string, day:string, start:string, end:string, breaks:string[][]=[] )=>toCalendarOccurrence( {shift, name, day, start: `${start}Z`, end: `${end}Z`, breaks: breaks.map( ([from, to])=>({start: `${from}Z`, end: `${to}Z`}) )} );
	it( 'maps an occurrence, and asks for a week of production days with no zone', ()=>{
		expect( occurrence( 7, 'Day', '2026-06-29', '2026-06-29T10:00:00', '2026-06-29T18:00:00', [['2026-06-29T14:00:00', '2026-06-29T14:30:00']] ) )
			.toEqual( {shift: 7, name: 'Day', day: '2026-06-29', start: utc( '2026-06-29T10:00:00' ), end: utc( '2026-06-29T18:00:00' ), breaks: [{start: utc( '2026-06-29T14:00:00' ), end: utc( '2026-06-29T14:30:00' )}]} );
		expect( occurrence( null, 'Extra Saturday', '2026-07-04', '2026-07-04T10:00:00', '2026-07-04T18:00:00' ).shift ).toBeUndefined();
		expect( toCalendarOccurrence( {name: 'Day', day: '2026-06-29', start: '2026-06-29T10:00:00Z', end: '2026-06-29T18:00:00Z'} ).breaks ).toEqual( [] );//asked for without them
		expect( weekRange( '2026-06-29', 360 ) ).toEqual( {start: '2026-06-29T06:00:00', end: '2026-07-06T06:00:00'} );
	} );
	//a closed exception from 08:00 to 10:00 on Friday split its Day shift in two, and an open one planned Saturday
	it( 'draws a week with a split shift and an open Saturday', ()=>{
		const bars = occurrenceBars( [
			occurrence( 7, 'Day', '2026-06-29', '2026-06-29T10:00:00', '2026-06-29T18:00:00', [['2026-06-29T14:00:00', '2026-06-29T14:30:00']] ),
			occurrence( 7, 'Day', '2026-07-03', '2026-07-03T10:00:00', '2026-07-03T12:00:00' ),
			occurrence( 7, 'Day', '2026-07-03', '2026-07-03T14:00:00', '2026-07-03T18:00:00', [['2026-07-03T14:00:00', '2026-07-03T14:30:00']] ),
			occurrence( null, 'Extra Saturday', '2026-07-04', '2026-07-04T10:00:00', '2026-07-04T18:00:00' )
		], '2026-06-29', NewYork, 360 );
		expect( bars.map( b=>[b.name, ...place( b ), b.times] ) ).toEqual( [
			['Day', 0, 0, 480, '06:00 - 14:00'],
			['Day', 4, 0, 120, '06:00 - 08:00'],
			['Day', 4, 240, 240, '10:00 - 14:00'],
			['Extra Saturday', 5, 0, 480, '06:00 - 14:00']
		] );
		expect( bars.map( b=>b.cuts ) ).toEqual( [[{top: 240, minutes: 30}], [], [{top: 240, minutes: 30}], []] );
		expect( bars.map( b=>[b.tone, b.shift] ) ).toEqual( [[1, 7], [1, 7], [1, 7], [openTone, undefined]] );
		expect( bars[3].detail ).toBe( 'Extra Saturday, 2026-07-04, 06:00 to 14:00, 8 h, an open exception' );
		expect( bars.every( b=>b.note===undefined ) ).toBe( true );
	} );
	it( "places a night by the wall clock:  into the next day's column, and off either end of the week", ()=>{
		const bars = occurrenceBars( [
			occurrence( 8, 'Night', '2026-06-28', '2026-06-29T02:00:00', '2026-06-29T10:00:00' ),//the Sunday before's, 22:00 to 06:00:  ends as the week begins
			occurrence( 9, 'Late', '2026-06-28', '2026-06-29T06:00:00', '2026-06-29T14:00:00' ),//the Sunday before's, 02:00 to 10:00:  four hours into Monday
			occurrence( 9, 'Late', '2026-06-29', '2026-06-30T06:00:00', '2026-06-30T14:00:00' ),//Monday's, into Tuesday
			occurrence( 9, 'Late', '2026-07-05', '2026-07-06T06:00:00', '2026-07-06T14:00:00' )//Sunday's, off the week
		], '2026-06-29', NewYork, 360 );
		expect( bars.map( b=>[b.key, ...place( b ), b.continued, b.continues] ) ).toEqual( [
			['1-0', 0, 0, 240, true, false],
			['2-0', 0, 1200, 240, false, true], ['2-1', 1, 0, 240, true, false],
			['3-0', 6, 1200, 240, false, true]
		] );
	} );
	//2026-03-08, 02:00 EST becomes 03:00 EDT;  2026-11-01, 02:00 EDT becomes 01:00 EST
	it( 'draws the night the clocks change as tall as any other, and says how long it ran', ()=>{
		const [spring] = occurrenceBars( [occurrence( 8, 'Night', '2026-03-07', '2026-03-08T03:00:00', '2026-03-08T10:00:00' )], '2026-03-02', NewYork, 360 );
		expect( [...place( spring ), spring.times, spring.note] ).toEqual( [5, 960, 480, '22:00 - 06:00', '7 h, clock change'] );
		expect( spring.detail ).toBe( 'Night, 2026-03-07, 22:00 to 06:00, 7 h across the clock change' );
		const [autumn] = occurrenceBars( [occurrence( 8, 'Night', '2026-10-31', '2026-11-01T02:00:00', '2026-11-01T11:00:00' )], '2026-10-26', NewYork, 360 );
		expect( [...place( autumn ), autumn.note] ).toEqual( [5, 960, 480, '9 h, clock change'] );
	} );
	it( 'takes seconds in a bound for what they are, not a clock change', ()=>{
		const [bar] = occurrenceBars( [occurrence( null, 'Extra', '2026-07-01', '2026-07-01T12:00:20', '2026-07-01T14:00:40' )], '2026-06-29', NewYork, 360 );
		expect( [bar.note, bar.detail] ).toEqual( [undefined, 'Extra, 2026-07-01, 08:00 to 10:00, 2 h, an open exception'] );
	} );
	it( 'bands the closed exceptions, the live ones', ()=>{
		const exception = ( id:number, name:string, start:string, finish:string, more:object={} )=>toCalendarException( {id, calendarId: 3, name, start: `${start}Z`, finish: `${finish}Z`, isOpen: false, ...more} );
		const bands = exceptionBands( [
			exception( 1, 'Maintenance', '2026-07-03T12:00:00', '2026-07-03T14:00:00' ),//Friday 08:00 to 10:00
			exception( 2, 'Independence Day', '2026-07-04T10:00:00', '2026-07-06T10:00:00' ),//the weekend's two production days
			exception( 3, 'Extra Saturday', '2026-07-04T10:00:00', '2026-07-04T18:00:00', {isOpen: true} ),
			exception( 4, 'Dropped', '2026-06-30T10:00:00', '2026-07-01T10:00:00', {deleted: '2026-06-01T00:00:00Z'} ),
			exception( 5, 'August', '2026-08-03T10:00:00', '2026-08-04T10:00:00' )//another week's
		], '2026-06-29', NewYork, 360 );
		expect( bands.map( b=>[b.key, b.name, ...place( b ), b.continued] ) ).toEqual( [
			['1-0', 'Maintenance', 4, 120, 120, false],
			['2-0', 'Independence Day', 5, 0, 1440, false], ['2-1', 'Independence Day', 6, 0, 1440, true]
		] );
	} );

	it( 'names a closed exception on its first day in the week, though it began in an earlier one', ()=>{
		const shutdown = toCalendarException( {id: 1, calendarId: 3, name: 'Shutdown', start: '2026-06-22T10:00:00Z', finish: '2026-07-01T10:00:00Z', isOpen: false} );
		expect( exceptionBands( [shutdown], '2026-06-29', NewYork, 360 ).map( b=>[b.column, b.continued] ) ).toEqual( [[0, false], [1, true]] );
	} );

	it( "holds a pattern's cycle as its form does, and sends the anchor as its day's start with no zone", ()=>{
		expect( toCycleForm( weekly, NewYork, '2026-07-20' ) ).toEqual( {rotation: false, days: 7, anchor: '2026-07-20'} );
		expect( toCycleForm( rotation, NewYork, '2026-07-20' ) ).toEqual( {rotation: true, days: 14, anchor: '2026-07-01'} );
		expect( toCycleArgs( {rotation: true, days: 8, anchor: '2026-07-20'}, 360 ) ).toEqual( {cycleDays: 8, cycleStart: '2026-07-20T06:00:00'} );
		expect( toCycleArgs( {rotation: false, days: 8, anchor: '2026-07-20'}, 360 ) ).toEqual( {cycleDays: 7, cycleStart: null} );//the week has no anchor
		expect( cycleArgsOf( rotation, NewYork ) ).toEqual( toCycleArgs( toCycleForm( rotation, NewYork, '' ), 360 ) );//an edit that changes nothing sends nothing
		expect( cycleArgsOf( weekly, NewYork ) ).toEqual( {cycleDays: 7, cycleStart: null} );
		expect( cycleProblem( {rotation: false, days: 0, anchor: ''} ) ).toBeUndefined();
		expect( cycleProblem( {rotation: true, days: 14, anchor: '2026-07-01'} ) ).toBeUndefined();
		expect( cycleProblem( {rotation: true, days: 32, anchor: '2026-07-01'} ) ).toBe( 'A rotation is 1 to 31 days long.' );
		expect( cycleProblem( {rotation: true, days: <number><unknown>null, anchor: '2026-07-01'} ) ).toContain( '1 to 31' );//a number input emptied
		expect( cycleProblem( {rotation: true, days: 14, anchor: '' } ) ).toBe( 'A rotation needs the date of its day 1.' );
	} );
} );