import { ISlugRow, SlugRow, SlugRowProps } from './ql/slug-row';

//The shift calendar as the pages hold it (docs/calendar/spec.md *Tables*):  the calendar's row, its patterns, their shifts and its
//exceptions, each from its own flat read (CalendarService), with the conversions between what the server keeps - instants, and
//minutes from the production day's start - and what a form shows, a wall-clock time in the calendar's zone.  The browser's Intl
//does every zone conversion, so nothing here needs a zone library.
export const minutesPerDay = 1440;

export class Calendar extends SlugRow<Calendar>{
	constructor( obj:CalendarProps ){
		super( Calendar.typeName, <SlugRowProps>obj );
		this.timeZone = obj.timeZone || undefined;
		this.dayStart = obj.dayStart===undefined ? 0 : toMinuteOfDay( obj.dayStart ) ?? null;
	}
	override get canSave():boolean{ return super.canSave && !!this.timeZone && this.dayStart!=null; }
	override equals( row:Partial<ISlugRow> ):boolean{
		const o = <Partial<Calendar>>row;
		return super.equals( row ) && this.timeZone==o.timeZone && this.dayStart==o.dayStart;
	}
	override mutationArgs( original:Calendar ){
		const args:Partial<Calendar> = super.mutationArgs( original );
		if( this.timeZone!=original?.timeZone )
			args.timeZone = this.timeZone;
		if( this.dayStart!=original?.dayStart )
			args.dayStart = this.dayStart;
		return args;
	}
	get properties():Calendar{ return new Calendar( this ); }
	timeZone?:string;
	dayStart:number|null;//minutes after local midnight, midnight on a new row;  null while the form's time is blank
	static typeName = 'Calendar';
}
//a QL row, or a Calendar with the form's edits
export type CalendarProps = Partial<SlugRowProps> & { timeZone?:string|null; dayStart?:number|string|null };
export type CalendarRef = { id:number; name:string; slug:string; timeZone:string; dayStart:number };
export type CalendarPattern = { id:number; calendarId:number; effective?:Date; cycleDays:number; cycleStart?:Date; deleted?:Date };
export type CalendarBreak = { start:number; minutes:number };//start:  minutes after the shift's
export type CalendarShift = { id:number; patternId:number; name:string; days:number; start:number; minutes:number; breaks:CalendarBreak[] };
export type CalendarException = { id:number; calendarId:number; name:string; start:Date; finish:Date; isOpen:boolean; deleted?:Date };

const toDate = ( v:unknown ):Date|undefined=>{
	const d = v ? new Date( <string>v ) : undefined;
	return d && !isNaN( d.getTime() ) ? d : undefined;
};
function toMinuteOfDay( v:unknown ):number|undefined{
	const n = v===null || v==='' ? NaN : Number( v );
	return Number.isInteger( n ) && n>=0 && n<minutesPerDay ? n : undefined;
}
export function toCalendarRef( json:any ):CalendarRef{ return { id: Number( json.id ), name: String( json.name ?? '' ), slug: String( json.slug ?? '' ), timeZone: String( json.timeZone ?? '' ), dayStart: Number( json.dayStart ?? 0 ) }; }
export function toCalendarPattern( json:any ):CalendarPattern{
	return { id: Number( json.id ), calendarId: Number( json.calendarId ), effective: toDate( json.effective ), cycleDays: Number( json.cycleDays ?? 7 ), cycleStart: toDate( json.cycleStart ), deleted: toDate( json.deleted ) };
}
//`breaks` is json in a text column, null for none:  text that isn't a list of them reads as none
export function toCalendarBreaks( text:unknown ):CalendarBreak[]{
	if( typeof text!='string' || !text )
		return [];
	try{
		const list = JSON.parse( text );
		return Array.isArray( list ) ? list.filter( b=>b && Number.isFinite( Number(b.start) ) && Number.isFinite( Number(b.minutes) ) ).map( b=>({start: Number( b.start ), minutes: Number( b.minutes )}) ) : [];
	}
	catch{
		return [];
	}
}
export function toCalendarShift( json:any ):CalendarShift{
	return { id: Number( json.id ), patternId: Number( json.calendarPatternId ), name: String( json.name ?? '' ), days: Number( json.days ?? 0 ), start: Number( json.start ?? 0 ), minutes: Number( json.minutes ?? 0 ), breaks: toCalendarBreaks( json.breaks ) };
}
export function toCalendarException( json:any ):CalendarException{
	return { id: Number( json.id ), calendarId: Number( json.calendarId ), name: String( json.name ?? '' ), start: toDate( json.start )!, finish: toDate( json.finish )!, isOpen: !!json.isOpen, deleted: toDate( json.deleted ) };
}

//A time of day as <input type="time"> holds it, 06:00, and as the columns do, minutes after midnight
export function minutesToTime( minutes:number ):string{
	const m = ( (Math.trunc( minutes ) % minutesPerDay) + minutesPerDay ) % minutesPerDay;
	return `${String( Math.floor(m/60) ).padStart( 2, '0' )}:${String( m%60 ).padStart( 2, '0' )}`;
}
export function timeToMinutes( text:string|null|undefined ):number|undefined{
	const m = /^(\d{1,2}):(\d{2})(?::\d{2}(?:\.\d+)?)?$/.exec( text?.trim() ?? '' );
	const hours = m ? +m[1] : NaN, minutes = m ? +m[2] : NaN;
	return hours<24 && minutes<60 ? hours*60+minutes : undefined;
}
//2 h 30 min
export function durationText( minutes:number ):string{
	const h = Math.floor( minutes/60 ), m = minutes%60;
	return [h ? `${h} h` : '', m || !h ? `${m} min` : ''].filter( s=>s ).join( ' ' );
}

//The wall clock in `zone` at an instant, as milliseconds of a UTC reading of it:  what the zone's offset is taken from
const formats = new Map<string,Intl.DateTimeFormat>();
function wallClock( instant:number, zone:string ):number{
	let format = formats.get( zone );
	if( !format )
		formats.set( zone, format = new Intl.DateTimeFormat( 'en-US', {timeZone: zone, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', second: 'numeric'} ) );
	const p:Record<string,number> = {};
	for( const part of format.formatToParts( instant ) )
		p[part.type] = +part.value;
	return Date.UTC( p['year'], p['month']-1, p['day'], p['hour'], p['minute'], p['second'] );
}
export function isTimeZone( zone:string ):boolean{
	try{ new Intl.DateTimeFormat( 'en-US', {timeZone: zone} ); return true; }
	catch{ return false; }
}
//An instant as a local date-time in `zone` with no zone in the text, 2026-07-03T18:00:  what <input type="datetime-local"> holds,
//and with its seconds what a mutation sends, which the server reads in the calendar's zone (spec *Validation*)
export function zoneLocal( instant:Date, zone:string ):string{
	const text = new Date( wallClock( instant.getTime(), zone ) ).toISOString();
	return text.substring( 0, text.substring( 17, 19 )=='00' ? 16 : 19 );
}
export function zoneLess( local:string ):string{ return local.length==16 ? `${local}:00` : local; }
//The instant a zone-less local date-time names in `zone`, as the server resolves one (choose::earliest):  the first of a time the
//clocks repeat, and for a time they skip the instant they jumped.  None for text that isn't one.
export function zoneInstant( local:string, zone:string ):Date|undefined{
	const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?$/.exec( local );
	if( !m )
		return undefined;
	const wall = Date.UTC( +m[1], +m[2]-1, +m[3], +m[4], +m[5], +(m[6] ?? 0) );
	if( isNaN( wall ) )
		return undefined;
	const day = 86400000;
	const offset = ( t:number )=>wallClock( t, zone )-t;
	//the offsets on either side of a change near the time:  each candidate is the time read under one of them
	const candidates = [wall-offset( wall-day ), wall-offset( wall+day )].filter( t=>wallClock( t, zone )==wall );
	if( candidates.length )
		return new Date( Math.min( ...candidates ) );
	let before = wall-day, after = wall+day;//skipped:  the change is between them, found to the second
	const was = offset( before );
	while( after-before>1000 ){
		const mid = before+Math.floor( (after-before)/2000 )*1000;
		if( offset( mid )==was )
			before = mid;
		else
			after = mid;
	}
	return new Date( after );
}
//a date, 2026-07-04, `days` on
export function addDays( date:string, days:number ):string{
	const [y, m, d] = date.split( '-' ).map( Number );
	return new Date( Date.UTC( y, m-1, d+days ) ).toISOString().substring( 0, 10 );
}

//A pattern's cycle is the week, Monday first, unless it is a rotation:  another length, or a week anchored on a date
export const weekdays = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
export function isWeekly( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'> ):boolean{ return pattern.cycleDays==7 && !pattern.cycleStart; }
export function cycleDayNames( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'> ):string[]{
	return isWeekly( pattern ) ? weekdays : Array.from( {length: pattern.cycleDays}, ( _, i )=>`Day ${i+1}` );
}
//`days` is a mask whose bit i is cycle day i.  By arithmetic:  a bitwise operator would read bit 31 as the sign.
export function maskDays( mask:number, cycleDays:number ):number[]{
	const days:number[] = [];
	for( let i=0; i<cycleDays; ++i ){
		if( Math.floor( mask/2**i )%2 )
			days.push( i );
	}
	return days;
}
export function daysMask( days:readonly number[] ):number{ return [...new Set( days )].reduce( ( mask, i )=>mask+2**i, 0 ); }
//A shift's days in words, for a bar's tooltip and a screen reader
export function daysDetail( mask:number, pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'> ):string{
	const names = cycleDayNames( pattern );
	return maskDays( mask, pattern.cycleDays ).map( i=>names[i] ).join( ', ' ) || 'No days';
}

//A shift's times are minutes after the production day's start, so a break's on the wall clock is the day's start on
export function breaksText( shift:Pick<CalendarShift,'start'|'breaks'>, dayStart:number ):string{
	return shift.breaks.map( b=>`${minutesToTime( dayStart+shift.start+b.start )} (${durationText( b.minutes )})` ).join( ', ' );
}

//The pattern a calendar runs by at an instant (spec *Resolution*):  the latest live one whose `effective` is not after it, a first
//pattern having none, and the lowest id should two tie
export function patternInEffect( patterns:readonly CalendarPattern[], at:Date ):CalendarPattern|undefined{
	let found:CalendarPattern|undefined;
	for( const p of patterns ){
		const from = p.effective?.getTime() ?? -Infinity;
		if( p.deleted || from>at.getTime() )
			continue;
		const best = found?.effective?.getTime() ?? -Infinity;
		if( !found || from>best || (from==best && p.id<found.id) )
			found = p;
	}
	return found;
}
//A pattern's `effective` is the instant its first production day starts, so that day's date is the instant's in the zone
export function patternDate( pattern:Pick<CalendarPattern,'effective'>, zone:string ):string|undefined{
	return pattern.effective ? zoneLocal( pattern.effective, zone ).substring( 0, 10 ) : undefined;
}

//An exception over whole production days, each from the day's start to the next day's:  the days a range of local date-times
//covers, last inclusive, or none where either end is inside a day;  and back, the range of `from` to `to`
export function wholeDays( start:string, finish:string, dayStart:number ):{from:string; to:string}|undefined{
	const time = minutesToTime( dayStart );
	const from = start.substring( 0, 10 ), until = finish.substring( 0, 10 );
	return start.substring( 11, 16 )==time && finish.substring( 11, 16 )==time && start.length==16 && finish.length==16 && until>from ? { from, to: addDays( until, -1 ) } : undefined;
}
//the production days a range of local date-times touches, last inclusive:  its end is exclusive, so one on a day's start isn't in that day
export function productionDays( start:string, finish:string, dayStart:number ):{from:string; to:string}{
	const day = ( local:string, less:number )=>{
		const at = Date.parse( `${local}Z` );
		return isNaN( at ) ? local.substring( 0, 10 ) : new Date( at-dayStart*60000-less ).toISOString().substring( 0, 10 );
	};
	const from = day( start, 0 ), to = day( finish, 1 );
	return { from, to: to<from ? from : to };
}
export function wholeDayRange( from:string, to:string, dayStart:number ):{start:string; finish:string}{
	const time = minutesToTime( dayStart );
	return { start: `${from}T${time}`, finish: `${addDays( to, 1 )}T${time}` };
}

//The form's shift, in wall-clock times:  what the dialog edits and the checks below read
export type ShiftForm = { name:string; days:boolean[]; start:string; end:string; breaks:{start:string; minutes:number}[] };
export type ShiftArgs = Pick<CalendarShift,'name'|'days'|'start'|'minutes'> & { breaks:string|null };
export function toShiftForm( shift:Pick<CalendarShift,'name'|'days'|'start'|'minutes'|'breaks'>, pattern:Pick<CalendarPattern,'cycleDays'>, dayStart:number ):ShiftForm{
	const set = maskDays( shift.days, pattern.cycleDays );
	return {
		name: shift.name,
		days: Array.from( {length: pattern.cycleDays}, ( _, i )=>set.includes( i ) ),
		start: minutesToTime( dayStart+shift.start ),
		end: minutesToTime( dayStart+shift.start+shift.minutes ),
		breaks: shift.breaks.map( b=>({start: minutesToTime( dayStart+shift.start+b.start ), minutes: b.minutes}) )
	};
}
//The form's length:  to the end time, into the next day where it is not after the start, so the same time is 24 hours
export function shiftMinutes( form:Pick<ShiftForm,'start'|'end'> ):number|undefined{
	const start = timeToMinutes( form.start ), end = timeToMinutes( form.end );
	return start==undefined || end==undefined ? undefined : ( (end-start+minutesPerDay-1) % minutesPerDay )+1;
}
//What is wrong with the form, as the server would refuse it (spec *Validation*), or nothing.  The overlap with the pattern's other
//shifts is the server's to find.
export function shiftProblem( form:ShiftForm ):string|undefined{
	const start = timeToMinutes( form.start ), minutes = shiftMinutes( form );
	if( !form.name.trim() )
		return 'A shift needs a name.';
	if( !form.days.some( d=>d ) )
		return 'Pick the days the shift runs.';
	if( start==undefined || minutes==undefined )
		return 'A shift needs its start and end times.';
	const breaks:CalendarBreak[] = [];
	for( const b of form.breaks ){
		const at = timeToMinutes( b.start );
		if( at==undefined || !Number.isInteger( b.minutes ) || b.minutes<1 )
			return 'A break needs its start and a length in minutes.';
		const offset = ( at-start+minutesPerDay ) % minutesPerDay;
		if( offset+b.minutes>minutes )
			return `The break at ${b.start} is outside the shift.`;
		breaks.push( {start: offset, minutes: b.minutes} );
	}
	breaks.sort( ( a, b )=>a.start-b.start );
	const second = breaks.findIndex( ( b, i )=>i>0 && b.start<breaks[i-1].start+breaks[i-1].minutes );
	return second>0 ? `The breaks at ${minutesToTime( start+breaks[second-1].start )} and ${minutesToTime( start+breaks[second].start )} overlap.` : undefined;
}
//The columns a valid form makes:  the start from the production day's, the breaks from the shift's, in order
export function toShiftArgs( form:ShiftForm, dayStart:number ):ShiftArgs{
	const start = timeToMinutes( form.start )!;
	const breaks = form.breaks.map( b=>({start: ( timeToMinutes( b.start )!-start+minutesPerDay ) % minutesPerDay, minutes: b.minutes}) ).sort( ( a, b )=>a.start-b.start );
	return { name: form.name.trim(), days: daysMask( form.days.flatMap( ( on, i )=>on ? [i] : [] ) ), start: ( start-dayStart+minutesPerDay ) % minutesPerDay, minutes: shiftMinutes( form )!, breaks: breaks.length ? JSON.stringify( breaks ) : null };
}
export function shiftArgsOf( shift:Pick<CalendarShift,'name'|'days'|'start'|'minutes'|'breaks'> ):ShiftArgs{
	return { name: shift.name, days: shift.days, start: shift.start, minutes: shift.minutes, breaks: shift.breaks.length ? JSON.stringify( shift.breaks ) : null };
}

//The form's exception:  whole production days, `from` to `to` inclusive, or the two local date-times
export type ExceptionForm = { name:string; whole:boolean; from:string; to:string; start:string; finish:string; isOpen:boolean };
//an exception's columns as a mutation sends them:  the bounds local date-times with no zone, which the server reads in the calendar's
export type ExceptionArgs = { name:string; start:string; finish:string; isOpen:boolean };
//`exception`'s form, or a new one's:  today's production day, closed
export function toExceptionForm( exception:Pick<CalendarException,'name'|'start'|'finish'|'isOpen'>|undefined, zone:string, dayStart:number, now=new Date() ):ExceptionForm{
	const today = productionDay( now, zone, dayStart );
	const range = exception ? { start: zoneLocal( exception.start, zone ), finish: zoneLocal( exception.finish, zone ) } : wholeDayRange( today, today, dayStart );
	const days = wholeDays( range.start, range.finish, dayStart );
	return { name: exception?.name ?? '', whole: !!days, from: days?.from ?? range.start.substring( 0, 10 ), to: days?.to ?? range.finish.substring( 0, 10 ), ...range, isOpen: exception?.isOpen ?? false };
}
//the form's range as local date-times, whichever way it is entered
export function exceptionRange( form:ExceptionForm, dayStart:number ):{start:string; finish:string}{
	return form.whole ? wholeDayRange( form.from, form.to, dayStart ) : { start: form.start, finish: form.finish };
}
export const isDate = ( text:string ):boolean=>/^\d{4}-\d{2}-\d{2}$/.test( text ) && addDays( text, 0 )==text;
export function exceptionProblem( form:ExceptionForm, zone:string, dayStart:number ):string|undefined{
	if( !form.name.trim() )
		return 'An exception needs a name.';
	if( form.whole && (!isDate( form.from ) || !isDate( form.to )) )
		return 'An exception needs its first and last day.';
	if( form.whole )
		return form.to<form.from ? 'The last day is before the first.' : undefined;
	const start = zoneInstant( form.start, zone ), finish = zoneInstant( form.finish, zone );
	if( !start || !finish )
		return 'An exception needs its start and end.';
	return finish.getTime()<=start.getTime() ? 'The end is not after the start.' : undefined;
}
export function toExceptionArgs( form:ExceptionForm, dayStart:number ):ExceptionArgs{
	const range = exceptionRange( form, dayStart );
	return { name: form.name.trim(), start: zoneLess( range.start ), finish: zoneLess( range.finish ), isOpen: form.isOpen };
}
export function exceptionArgsOf( exception:Pick<CalendarException,'name'|'start'|'finish'|'isOpen'>, zone:string ):ExceptionArgs{
	return { name: exception.name, start: zoneLess( zoneLocal( exception.start, zone ) ), finish: zoneLess( zoneLocal( exception.finish, zone ) ), isOpen: exception.isOpen };
}
//the args that differ from the row's, which an update sends
export function changedArgs<T extends object>( args:T, original:T ):Partial<T>{
	return <Partial<T>>Object.fromEntries( Object.entries( args ).filter( ( [key, value] )=>value!==(<any>original)[key] ) );
}

//The grid (#397, spec *The cycle*, *Web*):  a column per cycle day, or per day of a previewed week, the hours down from the day's
//start.  Everything a column draws is minutes from its top, the production day's start;  a span is minutes from the first column's.
export const gridSnap = 15;
export function minutesToPixels( minutes:number, hourHeight:number ):number{ return minutes*hourHeight/60; }
//a pointer's offset down a column as minutes from the day's start:  the nearest quarter hour, inside the day
export function pixelsToMinutes( pixels:number, hourHeight:number, snap=gridSnap ):number{
	return Math.min( minutesPerDay, Math.max( 0, Math.round( pixels*60/hourHeight/snap )*snap ) );
}
//the hours down the side, from the day's start
export function gridHours( dayStart:number ):string[]{ return Array.from( {length: 24}, ( _, i )=>minutesToTime( dayStart+i*60 ) ); }
//the hours a drag covers, either way it was dragged;  none for less than a quarter hour, a click
export function drawnSpan( from:number, to:number ):{start:number; minutes:number}|undefined{
	const start = Math.min( from, to ), minutes = Math.abs( to-from );
	return minutes<gridSnap ? undefined : { start, minutes };
}

const dayNumber = ( date:string ):number=>{ const [y, m, d] = date.split( '-' ).map( Number ); return Math.round( Date.UTC( y, m-1, d )/86400000 ); };
export function weekdayOf( date:string ):number{ return ( ((dayNumber( date )+3) % 7)+7 ) % 7; }//Monday 0:  day 0, 1970-01-01, was a Thursday
export function mondayOf( date:string ):string{ return addDays( date, -weekdayOf( date ) ); }
//the instant a production day starts, and the production day an instant is in:  the latest whose start is not after it (spec *Resolution*)
export function dayStartInstant( date:string, zone:string, dayStart:number ):Date|undefined{ return zoneInstant( `${date}T${minutesToTime( dayStart )}`, zone ); }
export function productionDay( instant:Date, zone:string, dayStart:number ):string{
	const date = zoneLocal( instant, zone ).substring( 0, 10 );
	const start = dayStartInstant( date, zone, dayStart );
	return start && start.getTime()>instant.getTime() ? addDays( date, -1 ) : date;
}
//A production day's place in a pattern's cycle:  the days from the anchor's, counted in dates so that a clock change shifts
//nothing, modulo the cycle, positive before the anchor too.  The week's anchor is any Monday.
export function cycleDayOf( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>, date:string, zone:string ):number{
	const anchor = pattern.cycleStart ? zoneLocal( pattern.cycleStart, zone ).substring( 0, 10 ) : '1970-01-05';
	const n = pattern.cycleDays || 7;
	return ( ((dayNumber( date )-dayNumber( anchor )) % n)+n ) % n;
}

//`short`:  the title where the columns are narrow, a weekday's three letters.  `sub`:  the line under it.  `current`:  the column of the chosen date, or of today.
export type GridColumn = { title:string; short:string; sub?:string; current:boolean };
const weekdayShort = weekdays.map( name=>name.substring( 0, 3 ) );
//A pattern's columns:  the week's seven by name, or a rotation's Day 1 to Day n with the weekday and date each falls on in the
//cycle `date` is in
export function patternColumns( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>, date:string, zone:string ):GridColumn[]{
	const current = cycleDayOf( pattern, date, zone );
	if( isWeekly( pattern ) )
		return weekdays.map( ( title, i )=>({title, short: weekdayShort[i], current: i==current}) );
	const first = addDays( date, -current );
	return Array.from( {length: pattern.cycleDays}, ( _, i )=>{
		const day = addDays( first, i );
		return { title: `Day ${i+1}`, short: `Day ${i+1}`, sub: `${weekdayShort[weekdayOf( day )]} ${day.substring( 5 )}`, current: i==current };
	} );
}
//a previewed week's, from its Monday, each with its date
export function weekColumns( monday:string, today?:string ):GridColumn[]{
	return weekdays.map( ( title, i )=>{
		const day = addDays( monday, i );
		return { title, short: weekdayShort[i], sub: day, current: day==today };
	} );
}

//A piece of a span in one column:  `continued` from the column before, or one that `continues` into the next, with the `cuts` -
//the breaks - that fall in it
export type GridSpan = { from:number; to:number };
export type GridCut = { top:number; minutes:number };
export type GridPiece = { column:number; top:number; minutes:number; cuts:GridCut[]; continued:boolean; continues:boolean };
//A span cut where each column ends, so that a shift running past the production day's end continues at the top of the next
//column.  `wrap`:  the columns are a cycle, whose last day runs into its first;  otherwise a piece outside them is left out.
export function gridPieces( span:GridSpan, cuts:readonly GridSpan[], columns:number, wrap:boolean ):GridPiece[]{
	const pieces:GridPiece[] = [];
	for( let from=span.from; from<span.to; ){
		const index = Math.floor( from/minutesPerDay ), base = index*minutesPerDay, to = Math.min( span.to, base+minutesPerDay );
		const column = wrap ? ( (index % columns)+columns ) % columns : index;
		if( column>=0 && column<columns ){
			pieces.push( {column, top: from-base, minutes: to-from, continued: from>span.from, continues: to<span.to,
				cuts: cuts.filter( c=>c.from<to && c.to>from ).map( c=>({top: Math.max( c.from, from )-base, minutes: Math.min( c.to, to )-Math.max( c.from, from )}) )} );
		}
		from = to;
	}
	return pieces;
}
//A bar:  a piece of a shift's run, or of an occurrence.  `detail` says it whole, for a tooltip and a screen reader.  `tone`
//tells neighbouring shifts apart, `openTone` being an open exception's.  `primary`:  the shift's first piece, its one tab stop.
export type GridBar = GridPiece & { key:string; name:string; times:string; note?:string; detail:string; tone:number; shift?:number; primary:boolean };
export type GridBand = GridPiece & { key:string; name:string };
export type GridDraw = { day:number; start:number; minutes:number };
export const openTone = 3;
//A pattern's shifts as bars:  one per shift on each day its bit is set, the breaks cut out
export function patternBars( shifts:readonly CalendarShift[], pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>, dayStart:number ):GridBar[]{
	const bars:GridBar[] = [];
	for( const s of shifts ){
		const start = minutesToTime( dayStart+s.start ), end = minutesToTime( dayStart+s.start+s.minutes ), breaks = breaksText( s, dayStart );
		const detail = `${s.name}, ${daysDetail( s.days, pattern )}, ${start} to ${end}, ${durationText( s.minutes )}${breaks ? `, breaks ${breaks}` : ''}`;
		let primary = true;
		for( const day of maskDays( s.days, pattern.cycleDays ) ){
			const from = day*minutesPerDay+s.start;
			const cuts = s.breaks.map( b=>({from: from+b.start, to: from+b.start+b.minutes}) );
			gridPieces( {from, to: from+s.minutes}, cuts, pattern.cycleDays, true ).forEach( ( piece, i )=>{
				bars.push( {...piece, key: `${s.id}-${day}-${i}`, name: s.name, times: `${start} - ${end}`, detail, tone: s.id % openTone, shift: s.id, primary} );
				primary = false;
			} );
		}
	}
	return bars;
}

//A resolved occurrence (spec *Resolution*):  a shift's on a production day, or an open exception's, which has no shift and
//carries the exception's name, with the closed exceptions already cut from it
export type CalendarOccurrence = { shift?:number; name:string; day:string; start:Date; end:Date; breaks:{start:Date; end:Date}[] };
export function toCalendarOccurrence( json:any ):CalendarOccurrence{
	const breaks = ( Array.isArray( json.breaks ) ? <any[]>json.breaks : [] ).map( b=>({start: toDate( b?.start )!, end: toDate( b?.end )!}) ).filter( b=>b.start && b.end );
	return { shift: json.shift==null ? undefined : Number( json.shift ), name: String( json.name ?? '' ), day: String( json.day ?? '' ), start: toDate( json.start )!, end: toDate( json.end )!, breaks };
}
//the week of production days from `monday`, as the local date-times `calendarOccurrences` reads in the calendar's zone
export function weekRange( monday:string, dayStart:number ):{start:string; end:string}{
	const range = wholeDayRange( monday, addDays( monday, 6 ), dayStart );
	return { start: zoneLess( range.start ), end: zoneLess( range.finish ) };
}
//an instant on the week's grid:  its wall clock in the zone, as minutes from the Monday's day start
function weekMinute( instant:Date, monday:string, zone:string, dayStart:number ):number{
	return Math.round( (wallClock( instant.getTime(), zone )-dayNumber( monday )*86400000)/60000 )-dayStart;
}
//A week's occurrences as bars, placed by the wall clock, as the hours down the side are:  the night the clocks change is as tall
//as any other, and its `note` says how long it ran
export function occurrenceBars( occurrences:readonly CalendarOccurrence[], monday:string, zone:string, dayStart:number ):GridBar[]{
	const at = ( instant:Date )=>weekMinute( instant, monday, zone, dayStart );
	const clock = ( instant:Date )=>zoneLocal( instant, zone ).substring( 11, 16 );
	return occurrences.flatMap( ( o, index )=>{
		const from = at( o.start ), to = Math.max( at( o.end ), from+1 );
		const elapsed = Math.round( (o.end.getTime()-o.start.getTime())/60000 );
		//the wall clock ran longer or shorter than the time did;  a bound's seconds are no change of the clocks
		const changed = Math.abs( wallClock( o.end.getTime(), zone )-wallClock( o.start.getTime(), zone )-(o.end.getTime()-o.start.getTime()) )>=60000;
		const note = changed ? `${durationText( elapsed )}, clock change` : undefined;
		const detail = `${o.name}, ${o.day}, ${clock( o.start )} to ${clock( o.end )}, ${durationText( elapsed )}${note ? ' across the clock change' : ''}${o.shift==undefined ? ', an open exception' : ''}`;
		return gridPieces( {from, to}, o.breaks.map( b=>({from: at( b.start ), to: at( b.end )}) ), 7, false )
			.map( ( piece, i )=>({...piece, key: `${index}-${i}`, name: o.name, times: `${clock( o.start )} - ${clock( o.end )}`, note, detail, tone: o.shift==undefined ? openTone : o.shift % openTone, shift: o.shift, primary: i==0}) );
	} );
}
//the live closed exceptions as bands behind them, so a holiday shows as more than a gap.  The first piece in the week is the one
//named, though the range began in an earlier week.
export function exceptionBands( exceptions:readonly CalendarException[], monday:string, zone:string, dayStart:number ):GridBand[]{
	return exceptions.filter( e=>!e.deleted && !e.isOpen ).flatMap( e=>
		gridPieces( {from: weekMinute( e.start, monday, zone, dayStart ), to: weekMinute( e.finish, monday, zone, dayStart )}, [], 7, false ).map( ( piece, i )=>({...piece, continued: i>0, key: `${e.id}-${i}`, name: e.name}) ) );
}

//A pattern's cycle as its form holds it:  the week, or a rotation of `days` whose day 1 was `anchor`, a date
export type CycleForm = { rotation:boolean; days:number; anchor:string };
export type CycleArgs = { cycleDays:number; cycleStart:string|null };
//`anchor`:  a weekly pattern's, should it become a rotation - a Monday keeps its shifts on the days they are on
export function toCycleForm( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>, zone:string, anchor:string ):CycleForm{
	return { rotation: !isWeekly( pattern ), days: pattern.cycleDays, anchor: pattern.cycleStart ? zoneLocal( pattern.cycleStart, zone ).substring( 0, 10 ) : anchor };
}
export function cycleProblem( form:CycleForm ):string|undefined{
	if( !form.rotation )
		return undefined;
	if( !Number.isInteger( form.days ) || form.days<1 || form.days>31 )
		return 'A rotation is 1 to 31 days long.';
	return isDate( form.anchor ) ? undefined : 'A rotation needs the date of its day 1.';
}
//the columns:  the anchor the start of its production day, a local time with no zone, and none for the week
export function toCycleArgs( form:CycleForm, dayStart:number ):CycleArgs{
	return form.rotation ? { cycleDays: form.days, cycleStart: `${form.anchor}T${minutesToTime( dayStart )}:00` } : { cycleDays: 7, cycleStart: null };
}
export function cycleArgsOf( pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>, zone:string ):CycleArgs{
	return { cycleDays: pattern.cycleDays, cycleStart: pattern.cycleStart ? zoneLess( zoneLocal( pattern.cycleStart, zone ) ) : null };
}