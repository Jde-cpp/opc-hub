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
//A shift's days as its table shows them:  the week's seven letters with a dot for a day off, where position tells the two Ts and
//Ss apart, and a rotation's day numbers from 1, runs folded - 1-2, 5-7
export function daysText( mask:number, pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'> ):string{
	const days = maskDays( mask, pattern.cycleDays );
	if( isWeekly( pattern ) )
		return weekdays.map( ( name, i )=>days.includes( i ) ? name[0] : '·' ).join( ' ' );
	const runs:string[] = [];
	for( let i=0; i<days.length; ++i ){
		const first = days[i];
		while( days[i+1]==days[i]+1 )
			++i;
		runs.push( days[i]==first ? `${first+1}` : `${first+1}-${days[i]+1}` );
	}
	return runs.join( ', ' );
}
//the same in words, for a tooltip and a screen reader
export function daysDetail( mask:number, pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'> ):string{
	const names = cycleDayNames( pattern );
	return maskDays( mask, pattern.cycleDays ).map( i=>names[i] ).join( ', ' ) || 'No days';
}

//A shift's times are minutes after the production day's start, so its wall-clock start is the day's start on
export function shiftClock( shift:Pick<CalendarShift,'start'>, dayStart:number ):string{ return minutesToTime( dayStart+shift.start ); }
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
	const today = zoneLocal( new Date( now.getTime()-dayStart*60000 ), zone ).substring( 0, 10 );
	const range = exception ? { start: zoneLocal( exception.start, zone ), finish: zoneLocal( exception.finish, zone ) } : wholeDayRange( today, today, dayStart );
	const days = wholeDays( range.start, range.finish, dayStart );
	return { name: exception?.name ?? '', whole: !!days, from: days?.from ?? range.start.substring( 0, 10 ), to: days?.to ?? range.finish.substring( 0, 10 ), ...range, isOpen: exception?.isOpen ?? false };
}
//the form's range as local date-times, whichever way it is entered
export function exceptionRange( form:ExceptionForm, dayStart:number ):{start:string; finish:string}{
	return form.whole ? wholeDayRange( form.from, form.to, dayStart ) : { start: form.start, finish: form.finish };
}
const isDate = ( text:string ):boolean=>/^\d{4}-\d{2}-\d{2}$/.test( text ) && addDays( text, 0 )==text;
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