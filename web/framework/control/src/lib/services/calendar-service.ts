import { Service } from '@angular/core';
import { CalendarException, CalendarPattern, CalendarRef, CalendarShift, ExceptionArgs, ShiftArgs, toCalendarException, toCalendarPattern, toCalendarRef, toCalendarShift } from '../model/calendar';

//the application server, as far as the calls below need it:  AppService in the pages, a stub in a spec
export type CalendarReader = { query<T>( ql:string, vars?:any ):Promise<T> };
export type CalendarPoster = { postQL<T>( ql:string, vars?:any ):Promise<T> };

//The calendar tables for the pages (#396, docs/calendar/spec.md *Reads*):  four flat reads, since the generic select joins no
//one-to-many child, each asking for the live rows where its table has `deleted` - or every row, for a tab showing the deleted ones -
//and the children's edits, each a table mutation the application server validates (CalendarMAwait), by POST as every mutation goes.
//A calendar's own row goes through DetailPage's save.
@Service()
export class CalendarService{
	async calendars( reader:CalendarReader ):Promise<CalendarRef[]>{
		const data = await reader.query<any>( CalendarService.calendarsQuery );
		return ( <any[]>(data?.calendars ?? []) ).map( toCalendarRef );
	}
	static readonly calendarsQuery = 'calendars( deleted: null ){ id name slug timeZone dayStart }';
	async patterns( reader:CalendarReader, calendarId:number, deleted=false ):Promise<CalendarPattern[]>{
		const data = await reader.query<any>( deleted ? CalendarService.allPatternsQuery : CalendarService.patternsQuery, {calendarId} );
		return ( <any[]>(data?.calendarPatterns ?? []) ).map( toCalendarPattern );
	}
	static readonly patternsQuery = 'calendarPatterns( calendarId: $calendarId, deleted: null ){ id calendarId effective cycleDays cycleStart }';
	static readonly allPatternsQuery = 'calendarPatterns( calendarId: $calendarId ){ id calendarId effective cycleDays cycleStart deleted }';
	//a shift has no `deleted`:  it leaves by purge
	async shifts( reader:CalendarReader, patternId:number ):Promise<CalendarShift[]>{
		const data = await reader.query<any>( CalendarService.shiftsQuery, {patternId} );
		return ( <any[]>(data?.calendarShifts ?? []) ).map( toCalendarShift );
	}
	static readonly shiftsQuery = 'calendarShifts( calendarPatternId: $patternId ){ id calendarPatternId name days start minutes breaks }';
	async exceptions( reader:CalendarReader, calendarId:number, deleted=false ):Promise<CalendarException[]>{
		const data = await reader.query<any>( deleted ? CalendarService.allExceptionsQuery : CalendarService.exceptionsQuery, {calendarId} );
		return ( <any[]>(data?.calendarExceptions ?? []) ).map( toCalendarException );
	}
	static readonly exceptionsQuery = 'calendarExceptions( calendarId: $calendarId, deleted: null ){ id calendarId name start finish isOpen }';
	static readonly allExceptionsQuery = 'calendarExceptions( calendarId: $calendarId ){ id calendarId name start finish isOpen deleted }';
	//the server's own zone, a new calendar's default (spec *Open questions* #8);  none from a server that doesn't say
	async serverTimeZone( reader:CalendarReader ):Promise<string|undefined>{
		const data = await reader.query<any>( CalendarService.timeZoneQuery );
		return data?.status?.timeZone || undefined;
	}
	static readonly timeZoneQuery = 'status{ timeZone }';

	async createShift( poster:CalendarPoster, patternId:number, shift:ShiftArgs ):Promise<void>{
		await CalendarService.send( poster, 'createCalendarShift', {calendarPatternId: patternId, ...shift}, '{ id }' );
	}
	//`changes`:  the columns to write, a null `breaks` clearing them
	async updateShift( poster:CalendarPoster, id:number, changes:Partial<ShiftArgs> ):Promise<void>{ await CalendarService.send( poster, 'updateCalendarShift', {id, ...changes} ); }
	async purgeShift( poster:CalendarPoster, id:number ):Promise<void>{ await CalendarService.send( poster, 'purgeCalendarShift', {id} ); }
	async createException( poster:CalendarPoster, calendarId:number, exception:ExceptionArgs ):Promise<void>{
		await CalendarService.send( poster, 'createCalendarException', {calendarId, ...exception}, '{ id }' );
	}
	async updateException( poster:CalendarPoster, id:number, changes:Partial<ExceptionArgs> ):Promise<void>{ await CalendarService.send( poster, 'updateCalendarException', {id, ...changes} ); }
	//A pattern's or an exception's soft delete, its restore and its purge, by the table's type:  each the generic mutation
	async remove( poster:CalendarPoster, type:CalendarChild, id:number, how:'delete'|'restore'|'purge' ):Promise<void>{ await CalendarService.send( poster, `${how}${type}`, {id} ); }

	//`name( a: $a, b: $b )`, each arg a variable of its own name
	static mutation( name:string, args:Record<string,unknown>, result='' ):{ql:string; vars:Record<string,unknown>}{
		return { ql: `mutation ${name}( ${Object.keys( args ).map( key=>`${key}: $${key}` ).join( ', ' )} )${result}`, vars: args };
	}
	private static send( poster:CalendarPoster, name:string, args:Record<string,unknown>, result='' ):Promise<unknown>{
		const {ql, vars} = CalendarService.mutation( name, args, result );
		return poster.postQL<unknown>( ql, vars );
	}
}
export type CalendarChild = 'CalendarPattern'|'CalendarException';