import { addDays, breaksText, Calendar, changedArgs, cycleDayNames, daysDetail, daysMask, daysText, durationText, exceptionArgsOf, exceptionProblem, exceptionRange, isWeekly, maskDays, minutesToTime, patternDate, patternInEffect, shiftArgsOf, shiftClock, ShiftForm, shiftMinutes, shiftProblem, timeToMinutes, toCalendarException, toCalendarPattern, toCalendarShift, toExceptionArgs, toExceptionForm, toShiftArgs, toShiftForm, wholeDayRange, wholeDays, zoneInstant, zoneLess, zoneLocal } from './calendar';

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
	it( "shows a shift's days as the week's letters, or a rotation's numbers", ()=>{
		expect( daysText( 31, weekly ) ).toBe( 'M T W T F · ·' );
		expect( daysText( 96, weekly ) ).toBe( '· · · · · S S' );
		expect( daysDetail( 96, weekly ) ).toBe( 'Saturday, Sunday' );
		expect( daysText( 0b1110011, rotation ) ).toBe( '1-2, 5-7' );
		expect( daysText( 0b100, rotation ) ).toBe( '3' );
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
		expect( shiftClock( day, 360 ) ).toBe( '06:00' );
		expect( shiftClock( night, 360 ) ).toBe( '22:00' );
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
	it( "fills its form on the calendar's wall clock, whole days where its range is", ()=>{
		expect( toExceptionForm( holiday, NewYork, 360 ) ).toEqual( {name: 'Independence Day', whole: true, from: '2026-07-04', to: '2026-07-04', start: '2026-07-04T06:00', finish: '2026-07-05T06:00', isOpen: false} );
		expect( toExceptionForm( halfDay, NewYork, 360 ) ).toEqual( {name: 'Stocktake', whole: false, from: '2026-07-03', to: '2026-07-03', start: '2026-07-03T18:00', finish: '2026-07-03T22:00', isOpen: true} );
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