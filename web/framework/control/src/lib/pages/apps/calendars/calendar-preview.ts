import { Component, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { addDays, CalendarException, CalendarOccurrence, exceptionBands, isDate, mondayOf, occurrenceBars, productionDay, weekColumns, weekRange } from '../../../model/calendar';
import { CalendarReader, CalendarService } from '../../../services/calendar-service';
import { CalendarGrid } from './calendar-grid';
import { CalendarRows } from './calendar-rows';

//A calendar's Preview (#397, docs/calendar/spec.md *Web*):  a week of production days, Monday first, as the server resolves it -
//`calendarOccurrences`, the pattern in effect each day with the exceptions applied - drawn on the grid in the calendar's zone, so
//a holiday and the night the clocks change are seen before a report depends on them.  An open exception's time is a bar under
//its name;  a closed one's range is hatched behind what it left.  The tab's content is made each time the tab is opened, so the
//week is read again after an edit on another tab.
@Component({
	selector: 'calendar-preview',
	templateUrl: './calendar-preview.html',
	styleUrl: './calendar-detail.scss',
	host: {style: 'display: block'},
	imports: [CalendarGrid, MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatTooltipModule]
})
export class CalendarPreview{
	constructor(){
		effect( ()=>{//the calendar, the week, or the zone or day start its bounds are read in, changed
			this.calendarId(); this.monday(); this.zone(); this.dayStart();
			untracked( ()=>this.refresh() );
		} );
	}
	ql = input.required<CalendarReader>();
	calendarId = input.required<number>();
	zone = input.required<string>();//the zone the times are read in, as the row was saved
	lacking = input<string>();//the calendar's zone, where this browser doesn't know it:  nothing can be placed
	dayStart = input( 0 );
	exceptions = input<CalendarException[]>( [] );//the calendar's, for the closed ones' bands
	#service = inject( CalendarService );

	now = signal( new Date() );
	week = signal<string|undefined>( undefined );//the Monday picked;  none, and this week's
	today = computed( ()=>productionDay( this.now(), this.zone(), this.dayStart() ) );
	monday = computed( ()=>this.week() ?? mondayOf( this.today() ) );
	sunday = computed( ()=>addDays( this.monday(), 6 ) );
	occurrences = new CalendarRows<CalendarOccurrence>();
	#shown:string|undefined;//the week `occurrences` holds the rows of
	columns = computed( ()=>weekColumns( this.monday(), this.today() ) );
	bars = computed( ()=>occurrenceBars( this.occurrences.rows(), this.monday(), this.zone(), this.dayStart() ) );
	bands = computed( ()=>exceptionBands( this.exceptions(), this.monday(), this.zone(), this.dayStart() ) );

	async refresh():Promise<void>{
		const id = this.calendarId(), monday = this.monday(), range = weekRange( monday, this.dayStart() );
		if( this.lacking() )
			return;
		if( `${id} ${monday}`!=this.#shown ){//none of another week's stay under this one's dates, should the read fail
			this.#shown = `${id} ${monday}`;
			this.occurrences.rows.set( [] );
		}
		await this.occurrences.read( ()=>this.#service.occurrences( this.ql(), id, range.start, range.end ),
			rows=>this.occurrences.rows.set( rows ),
			()=>this.calendarId()==id && this.monday()==monday );
	}
	//today taken afresh, and the week read again
	async reload():Promise<void>{
		const monday = this.monday();
		this.now.set( new Date() );
		if( this.monday()==monday )//else the week moved on, and the effect reads it
			await this.refresh();
	}
	//the week a date is in;  this week for none.  The field shows the week's Monday whatever day was picked.
	setWeek( input:{value:string} ):void{
		this.week.set( isDate( input.value ) ? mondayOf( input.value ) : undefined );
		input.value = this.monday();
	}
	step( weeks:number ):void{ this.week.set( addDays( this.monday(), 7*weeks ) ); }
}