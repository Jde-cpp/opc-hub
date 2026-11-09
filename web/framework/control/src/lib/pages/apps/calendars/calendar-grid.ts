import { Component, computed, input, output, signal } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import { MatTooltipModule } from '@angular/material/tooltip';
import { drawnSpan, GridBand, GridBar, GridColumn, GridDraw, gridHours, minutesPerDay, minutesToPixels, minutesToTime, pixelsToMinutes } from '../../../model/calendar';

//The calendar's grid (#397, docs/calendar/spec.md *The cycle*, *Web*):  a column per cycle day - or per day of a previewed week -
//and the hours down from the production day's start, a bar per shift with its breaks cut out, one that runs past the day's end
//continuing at the top of the next column.  The page lays the bars out (model/calendar.ts);  this draws them, minutes to pixels.
//`editable`:  a shift's bar is a button that asks for its form, and a drag down empty space asks for a new shift over the hours
//dragged, to the quarter hour.  A shift has one tab stop, its first bar, named for the whole of it;  the rest are for the pointer.
//Otherwise the bars are the picture alone, each saying itself to a screen reader.
@Component({
	selector: 'calendar-grid',
	templateUrl: './calendar-grid.html',
	styleUrl: './calendar-grid.scss',
	host: { '[style.--hour.px]': 'hourHeight()', '[style.--columns]': 'columns().length', '(document:keydown.escape)': 'drag.set( undefined )' },
	imports: [MatTooltipModule, NgTemplateOutlet]
})
export class CalendarGrid{
	columns = input.required<GridColumn[]>();
	dayStart = input( 0 );
	bars = input<GridBar[]>( [] );
	bands = input<GridBand[]>( [] );//closed exceptions, behind the bars
	editable = input( false );
	hourHeight = input( 28 );
	label = input( 'Shifts' );
	open = output<number>();//the shift of the bar picked
	draw = output<GridDraw>();//the hours dragged on a day

	hours = computed( ()=>gridHours( this.dayStart() ) );
	height = computed( ()=>this.px( minutesPerDay ) );
	tabStop = computed( ()=>!this.editable() || !this.bars().length );//where no bar takes the focus, the keyboard's way to scroll
	//what each column draws
	cells = computed( ()=>this.columns().map( ( column, i )=>({column, bars: this.bars().filter( b=>b.column==i ), bands: this.bands().filter( b=>b.column==i )}) ) );
	//a drag in flight:  its column, and where it began and is, in minutes from the day's start
	drag = signal<{column:number; from:number; to:number}|undefined>( undefined );
	drawing = computed( ()=>{
		const d = this.drag(), span = d ? drawnSpan( d.from, d.to ) : undefined;
		return d && span ? { column: d.column, ...span, text: `${minutesToTime( this.dayStart()+span.start )} - ${minutesToTime( this.dayStart()+span.start+span.minutes )}` } : undefined;
	} );
	px( minutes:number ):number{ return minutesToPixels( minutes, this.hourHeight() ); }

	//a touch scrolls the page:  Add shift is its way in.  A Mac's ctrl-click is button 0, and its context menu swallows the release.
	onPointerDown( e:PointerEvent, column:number ):void{
		if( !this.editable() || e.button!=0 || e.ctrlKey || e.pointerType=='touch' )
			return;
		const at = this.#minuteAt( e );
		(<Element>e.currentTarget).setPointerCapture?.( e.pointerId );
		this.drag.set( {column, from: at, to: at} );
		e.preventDefault();//no text selected under the drag
	}
	onPointerMove( e:PointerEvent ):void{
		const d = this.drag();
		if( d )
			this.drag.set( {...d, to: this.#minuteAt( e )} );
	}
	onPointerUp():void{
		const d = this.drag(), span = d ? drawnSpan( d.from, d.to ) : undefined;
		this.drag.set( undefined );
		if( d && span )
			this.draw.emit( {day: d.column, ...span} );
	}
	#minuteAt( e:PointerEvent ):number{ return pixelsToMinutes( e.clientY-(<Element>e.currentTarget).getBoundingClientRect().top, this.hourHeight() ); }
}