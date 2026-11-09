import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatRadioModule } from '@angular/material/radio';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { addDays, Calendar, CalendarException, CalendarPattern, CalendarShift, changedArgs, cycleArgsOf, dayStartInstant, exceptionArgsOf, GridDraw, isDate, isTimeZone, isWeekly, minutesPerDay, minutesToTime, mondayOf, patternBars, patternColumns, patternDate, patternInEffect, productionDay, shiftArgsOf, timeToMinutes, zoneLocal } from '../../../model/calendar';
import { AppService } from '../../../services/app/app-service';
import { CalendarChild, CalendarService } from '../../../services/calendar-service';
import { confirm } from '../../../shared/confirm/confirm-dialog';
import { errorMessage } from '../../../utils/errors';
import { DetailActions } from '../../detail-actions/detail-actions';
import { DetailPage } from '../../detail-page';
import { Properties } from '../../graphql/properties/properties';
import { CalendarChangeDialog } from './calendar-change-dialog';
import { CalendarCycleDialog } from './calendar-cycle-dialog';
import { CalendarExceptionDialog } from './calendar-exception-dialog';
import { CalendarGrid } from './calendar-grid';
import { CalendarPreview } from './calendar-preview';
import { CalendarRows } from './calendar-rows';
import { CalendarShiftDialog, CalendarShiftDialogData } from './calendar-shift-dialog';

//The fields the form leaves out:  the zone and the day start have controls of their own below it.  A constant, as the binding is
//read each check and a new array has Properties rebuild its fields.
const excluded = ['timeZone', 'dayStart'];
//what a calendar's delete costs beyond its row, since the application server can't see who plans by it (spec *Web*)
export const sharedCalendarLine = "Calendars are shared:  every gateway plans by them, and a machine on a deleted calendar is planned around the clock until it is pointed at another.";

//A shift calendar's page (#396, docs/calendar/spec.md *Web*), under the application server's page - a hub's is its gateway's.
//Properties is the row with its zone and day start, saved as any record is, a refusal said under the form.  Pattern lists the
//calendar's patterns, the one in effect on the date picked selected - today's, until one is - with its shifts on the grid (#397):
//a bar opens the shift's form, a drag on empty space or Add shift a new one's, Cycle sets the week or a rotation, and Change
//from… copies the pattern to a new one from a date.  Exceptions lists the holidays, shutdowns and extra days, and Preview draws a
//week as the server resolves it.  A shift's, a pattern's and an exception's edits are each a mutation of its own, applied as it
//is made, not held for Save.  Every time shows on the calendar's wall clock, in the zone the row was saved with, and goes back
//with no zone for the server to read there.
@Component({
	templateUrl: './calendar-detail.html',
	styleUrl: './calendar-detail.scss',
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content my-content calendar-detail'},
	imports: [CalendarGrid, CalendarPreview, DetailActions, MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatRadioModule, MatSelectModule, MatSlideToggleModule, MatTableModule, MatTabsModule, MatTooltipModule, Properties]
})
export class CalendarDetail extends DetailPage<Calendar>{
	constructor(){ super( 'calendar-detail' ); }

	protected override get ctor(){ return Calendar; }
	protected override onRow(){
		this.patterns.reset();
		this.shifts.reset();
		this.exceptions.reset();
		this.selectedId.set( undefined );
		this.date.set( undefined );
		if( this.row.id )
			this.refresh();
		else
			this.#offerServerZone();
	}
	protected override upsert():Calendar{ return new Calendar( this.properties() ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Calendar` : 'New Calendar'; }
	protected override saveFailed( e:unknown ):void{//said on Properties alone, and Save is on every tab
		this.#refusal.set( {of: this.properties(), text: errorMessage(e)} );
		this.tabIndex.set( 0 );
	}
	protected override get deleteWarning(){ return `${sharedCalendarLine}  Restore brings it back.`; }
	protected override get purgeWarning(){ return `Its patterns, shifts and exceptions go with it.  ${sharedCalendarLine}`; }
	ql = inject( AppService );
	#service = inject( CalendarService );

	readonly fieldOrder = ['slug', 'name', 'description'];
	readonly excludedFields = excluded;
	readonly patternColumns = ['from', 'cycle', 'state', 'actions'];
	readonly exceptionColumns = ['name', 'start', 'finish', 'kind', 'actions'];
	get calendar():Calendar{ return <Calendar>this.properties(); }
	//the server's refusal of the last Save, said while the form is as it was sent:  an edit answers it
	#refusal = signal<{of:unknown; text:string}|undefined>( undefined );
	saveError = computed( ()=>this.#refusal()?.of===this.properties() ? this.#refusal()!.text : undefined );
	serverZone = signal<string|undefined>( undefined );
	//the browser's zones, with UTC, which V8's list leaves out, and the row's and the server's where it lacks them:  the server's
	//refusal is the final word on a name
	zones = computed<string[]>( ()=>{
		const zones = new Set<string>( Intl.supportedValuesOf( 'timeZone' ) );
		for( const zone of ['UTC', this.calendar?.timeZone, this.serverZone()] ){
			if( zone )
				zones.add( zone );
		}
		return [...zones].sort();
	} );
	today = signal( new Date() );
	patterns = new CalendarRows<CalendarPattern>();
	shifts = new CalendarRows<CalendarShift>();
	exceptions = new CalendarRows<CalendarException>();
	showDeletedPatterns = signal( false );
	showDeletedExceptions = signal( false );
	date = signal<string|undefined>( undefined );//the production day picked;  none, and today's
	selectedId = signal<number|undefined>( undefined );//the pattern picked from the list;  none, and the one in effect on the date shows
	#shiftsOf:number|undefined;//the pattern `shifts` holds the rows of
	inEffect = computed( ()=>patternInEffect( this.patterns.rows(), this.today() ) );//now:  what the list's Status says
	dateText = computed( ()=>this.date() ?? productionDay( this.today(), this.zone(), this.dayStart ) );
	//the instant the pattern is picked at:  the picked day's start, or now
	#at = computed( ()=>{ const date = this.date(); return (date ? dayStartInstant( date, this.zone(), this.dayStart ) : undefined) ?? this.today(); } );
	selected = computed<CalendarPattern|undefined>( ()=>this.patterns.rows().find( p=>p.id==this.selectedId() ) ?? patternInEffect( this.patterns.rows(), this.#at() ) ?? this.patterns.rows()[0] );
	//the grid:  the selected pattern's cycle days, a rotation's under the dates of the cycle the picked day is in, and its shifts
	columns = computed( ()=>{ const p = this.selected(); return p ? patternColumns( p, this.dateText(), this.zone() ) : []; } );
	bars = computed( ()=>{ const p = this.selected(); return p ? patternBars( this.shifts.rows(), p, this.dayStart ) : []; } );

	//Properties
	//a new calendar's zone is the server's own, the plant's wall clock (spec *Open questions* #8), unless one is picked meanwhile
	async #offerServerZone():Promise<void>{
		const row = this.row;
		try{
			const zone = await this.#service.serverTimeZone( this.ql );
			if( !zone || row!==this.row )
				return;
			this.serverZone.set( zone );
			if( !this.calendar.timeZone )
				this.#edit( c=>{ c.timeZone = zone; } );
		}
		catch( e ){
			console.warn( "Could not read the server's time zone.", e );
		}
	}
	dayStartText():string{ return this.calendar.dayStart==null ? '' : minutesToTime( this.calendar.dayStart ); }
	setTimeZone( zone:string ):void{ this.#edit( c=>{ c.timeZone = zone; } ); }
	setDayStart( text:string ):void{ this.#edit( c=>{ c.dayStart = timeToMinutes( text ) ?? null; } ); }
	//a new row each change, so the page's dirty check sees it
	#edit( change:( c:Calendar )=>void ):void{
		const c = new Calendar( this.properties() );
		change( c );
		this.properties.set( c );
	}

	//The zone and day start every time on the page is read in:  the saved row's, which the stored instants were resolved under, not
	//an edit of them not saved yet.  UTC for a zone this browser lacks, which shows the times but can't enter an exception's, as the
	//server reads a zone-less time in the calendar's own.
	zoneLacking = computed( ()=>{ const zone = this.row?.timeZone; return !!zone && !isTimeZone( zone ); } );
	zone = computed( ()=>this.zoneLacking() || !this.row?.timeZone ? 'UTC' : this.row.timeZone );
	get dayStart():number{ return this.row.dayStart ?? 0; }
	clock( instant:Date ):string{ return zoneLocal( instant, this.zone() ).replace( 'T', ' ' ); }

	async refresh():Promise<void>{ await Promise.all( [this.refreshPatterns(), this.refreshExceptions()] ); }

	//Pattern
	async refreshPatterns():Promise<void>{
		const id = this.row?.id, deleted = this.showDeletedPatterns();
		if( !id )
			return;
		this.today.set( new Date() );
		await this.patterns.read( ()=>this.#service.patterns( this.ql, id, deleted ),
			rows=>this.patterns.rows.set( rows.sort( ( a, b )=>(a.effective?.getTime() ?? -Infinity)-(b.effective?.getTime() ?? -Infinity) || a.id-b.id ) ),
			()=>this.row?.id==id );
		await this.refreshShifts();
	}
	async refreshShifts():Promise<void>{
		const pattern = this.selected(), id = this.row?.id;
		if( !pattern )
			return this.shifts.reset();
		if( pattern.id!=this.#shiftsOf ){//none of another pattern's stay under this one's heading, should the read fail
			this.#shiftsOf = pattern.id;
			this.shifts.rows.set( [] );
		}
		await this.shifts.read( ()=>this.#service.shifts( this.ql, pattern.id ),
			rows=>this.shifts.rows.set( rows.sort( ( a, b )=>a.start-b.start || a.name.localeCompare( b.name ) ) ),
			()=>this.row?.id==id && this.selected()?.id==pattern.id );
	}
	async setShowDeletedPatterns( show:boolean ):Promise<void>{
		this.showDeletedPatterns.set( show );
		await this.refreshPatterns();
	}
	async select( pattern:CalendarPattern ):Promise<void>{
		if( pattern.id==this.selected()?.id )
			return;
		this.selectedId.set( pattern.id );
		await this.refreshShifts();
	}
	//The date picker picks the pattern in effect:  on the day picked, over one picked from the list;  blank, or today, is today
	//again.  `input`:  the field, which shows the day in use whatever was typed.
	async setDate( input:{value:string} ):Promise<void>{
		const now = new Date();
		this.today.set( now );
		this.date.set( isDate( input.value ) && input.value!=productionDay( now, this.zone(), this.dayStart ) ? input.value : undefined );
		input.value = this.dateText();
		this.selectedId.set( undefined );
		if( this.selected()?.id!=this.#shiftsOf )
			await this.refreshShifts();
	}
	//Change from…:  the pattern showing and its shifts copied to a new pattern from a day, which then shows.  The day offered is
	//the one picked, or tomorrow.
	async changeFrom():Promise<void>{
		const source = this.selected();
		if( !source )
			return;
		const zone = this.zone(), time = minutesToTime( this.dayStart );
		const copy = await CalendarChangeDialog.open( this.dialog, { source: this.fromText( source ).toLowerCase(), from: this.date() ?? addDays( this.dateText(), 1 ), zone, save: date=>this.#service.copyPattern( this.ql, source.id, `${date}T${time}:00` ) } );
		if( !copy )
			return;
		this.selectedId.set( copy.id );
		this.date.set( patternDate( copy, zone ) );
		await this.refreshPatterns();
	}
	//the selected pattern's cycle:  the week, or a rotation, whose day 1 is offered as the Monday of the picked day's week
	async editCycle():Promise<void>{
		const pattern = this.selected();
		if( !pattern )
			return;
		const zone = this.zone();
		if( await CalendarCycleDialog.open( this.dialog, { pattern, zone, dayStart: this.dayStart, anchor: mondayOf( this.dateText() ), save: args=>this.#service.updatePattern( this.ql, pattern.id, changedArgs( args, cycleArgsOf( pattern, zone ) ) ) } ) )
			await this.refreshPatterns();
	}
	fromText( p:CalendarPattern ):string{ return patternDate( p, this.zone() ) ?? 'The beginning'; }
	cycleText( p:CalendarPattern ):string{
		if( isWeekly( p ) )
			return 'Weekly';
		return `${p.cycleDays}-day cycle${p.cycleStart ? `, day 1 on ${zoneLocal( p.cycleStart, this.zone() ).substring( 0, 10 )}` : ''}`;
	}
	stateText( p:CalendarPattern ):string{
		if( p.deleted )
			return 'Deleted';
		if( p.id==this.inEffect()?.id )
			return 'In effect';
		return (p.effective?.getTime() ?? -Infinity)>this.today().getTime() ? 'Upcoming' : 'Replaced';
	}
	//a pattern's soft delete and its restore, as DetailPage has a record's:  the server refuses the calendar's only pattern
	async deletePattern( p:CalendarPattern ):Promise<void>{ await this.#remove( this.patterns, 'CalendarPattern', p.id, p.deleted ? 'restore' : 'delete', ()=>this.refreshPatterns() ); }
	async purgePattern( p:CalendarPattern ):Promise<void>{
		const which = p.effective ? `The pattern from ${this.fromText( p )}` : 'The first pattern';
		if( !await confirm( this.dialog, {title: 'Purge the pattern?', message: `${which} and its shifts will be permanently removed. This cannot be undone - a purged row has no Restore.`, confirm: 'Purge', destructive: true} ) )
			return;
		await this.#remove( this.patterns, 'CalendarPattern', p.id, 'purge', ()=>this.refreshPatterns() );
	}
	async #remove( rows:CalendarRows<any>, type:CalendarChild, id:number, how:'delete'|'restore'|'purge', then:()=>Promise<void> ):Promise<void>{
		await rows.act( async ()=>{
			await this.#service.remove( this.ql, type, id, how );
			await then();
		} );
	}

	//Shifts, of the selected pattern:  on the grid
	async addShift():Promise<void>{
		const last = Math.max( 0, ...this.shifts.rows().map( s=>s.start+s.minutes ) );//on from the latest, while the day has room
		await this.#newShift( {startAt: last<minutesPerDay ? last : 0} );
	}
	//the hours dragged on a day of the cycle, which the form opens on
	async drawShift( drawn:GridDraw ):Promise<void>{ await this.#newShift( {startAt: drawn.start, minutes: drawn.minutes, days: [drawn.day]} ); }
	async #newShift( at:Pick<CalendarShiftDialogData,'startAt'|'minutes'|'days'> ):Promise<void>{
		const pattern = this.selected();
		if( !pattern || this.shifts.busy() )//the grid stays live through an edit, so a bar keeps the focus its form hands back
			return;
		if( await CalendarShiftDialog.open( this.dialog, { pattern, dayStart: this.dayStart, ...at, save: args=>this.#service.createShift( this.ql, pattern.id, args ) } ) )
			await this.refreshShifts();
	}
	async openShift( id:number ):Promise<void>{
		const shift = this.shifts.rows().find( s=>s.id==id );
		if( shift && !this.shifts.busy() )
			await this.editShift( shift );
	}
	async editShift( shift:CalendarShift ):Promise<void>{
		const pattern = this.selected();
		if( !pattern )
			return;
		const done = await CalendarShiftDialog.open( this.dialog, { pattern, dayStart: this.dayStart, shift, save: args=>this.#service.updateShift( this.ql, shift.id, changedArgs( args, shiftArgsOf( shift ) ) ) } );
		if( done=='remove' )
			await this.removeShift( shift );
		else if( done )
			await this.refreshShifts();
	}
	//a purge:  a shift has no soft delete
	async removeShift( shift:CalendarShift ):Promise<void>{
		if( !await confirm( this.dialog, {title: `Remove ${shift.name}?`, message: `The shift is removed for good:  a shift has no Restore.  Every gateway's planned time changes with it, for as long as the pattern has been in effect.`, confirm: 'Remove', destructive: true} ) )
			return;
		await this.shifts.act( async ()=>{
			await this.#service.purgeShift( this.ql, shift.id );
			await this.refreshShifts();
		} );
	}

	//Exceptions
	async refreshExceptions():Promise<void>{
		const id = this.row?.id, deleted = this.showDeletedExceptions();
		if( !id )
			return;
		await this.exceptions.read( ()=>this.#service.exceptions( this.ql, id, deleted ),
			rows=>this.exceptions.rows.set( rows.sort( ( a, b )=>a.start.getTime()-b.start.getTime() || a.id-b.id ) ),
			()=>this.row?.id==id );
	}
	async setShowDeletedExceptions( show:boolean ):Promise<void>{
		this.showDeletedExceptions.set( show );
		await this.refreshExceptions();
	}
	async addException():Promise<void>{
		const id = this.row.id;
		if( await CalendarExceptionDialog.open( this.dialog, { zone: this.zone(), dayStart: this.dayStart, save: args=>this.#service.createException( this.ql, id, args ) } ) )
			await this.refreshExceptions();
	}
	async editException( exception:CalendarException ):Promise<void>{
		const zone = this.zone();
		if( await CalendarExceptionDialog.open( this.dialog, { zone, dayStart: this.dayStart, exception, save: args=>this.#service.updateException( this.ql, exception.id, changedArgs( args, exceptionArgsOf( exception, zone ) ) ) } ) )
			await this.refreshExceptions();
	}
	async deleteException( e:CalendarException ):Promise<void>{ await this.#remove( this.exceptions, 'CalendarException', e.id, e.deleted ? 'restore' : 'delete', ()=>this.refreshExceptions() ); }
	async purgeException( e:CalendarException ):Promise<void>{
		if( !await confirm( this.dialog, {title: `Purge ${e.name}?`, message: `${e.name} will be permanently removed. This cannot be undone - a purged row has no Restore.`, confirm: 'Purge', destructive: true} ) )
			return;
		await this.#remove( this.exceptions, 'CalendarException', e.id, 'purge', ()=>this.refreshExceptions() );
	}
}