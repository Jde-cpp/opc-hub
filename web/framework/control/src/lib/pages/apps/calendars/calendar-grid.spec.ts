import { TestBed } from '@angular/core/testing';
import { exceptionBands, GridBand, GridBar, GridColumn, GridDraw, occurrenceBars, patternBars, patternColumns, toCalendarException, toCalendarOccurrence, toCalendarShift, weekColumns } from '../../../model/calendar';
import { CalendarGrid } from './calendar-grid';

const NewYork = 'America/New_York';
const weekly = { cycleDays: 7 };
const day = toCalendarShift( {id: 7, calendarPatternId: 4, name: 'Day', days: 31, start: 0, minutes: 480, breaks: '[{"start":240,"minutes":30}]'} );
const night = toCalendarShift( {id: 8, calendarPatternId: 4, name: 'Night', days: 31, start: 960, minutes: 480, breaks: null} );
const late = toCalendarShift( {id: 9, calendarPatternId: 4, name: 'Late', days: 64, start: 1200, minutes: 480, breaks: null} );//Sunday 02:00, into Monday

//the grid over its inputs, with what it emits:  the week under a 06:00 day start unless given.  `at` is a pointer event `y`
//pixels down a column, which jsdom lays out at the top of the page.
const create = ( {columns=patternColumns( weekly, '2026-07-22', NewYork ), bars=<GridBar[]>[], bands=<GridBand[]>[], editable=false, hourHeight=<number|undefined>undefined}={} )=>{
	const fixture = TestBed.createComponent( CalendarGrid );
	const set = ( inputs:Record<string,unknown> )=>{
		for( const [name, value] of Object.entries( inputs ) )
			fixture.componentRef.setInput( name, value );
		fixture.detectChanges();
	};
	const grid = fixture.componentInstance, opened = vi.fn( ( _id:number )=>{} ), drawn = vi.fn( ( _draw:GridDraw )=>{} );
	grid.open.subscribe( opened );
	grid.draw.subscribe( drawn );
	set( {columns: <GridColumn[]>columns, dayStart: 360, bars, bands, editable, ...(hourHeight ? {hourHeight} : {})} );
	const root:HTMLElement = fixture.nativeElement;
	const column = ( index:number )=><HTMLElement>root.querySelectorAll( '.column' )[index];
	const at = ( target:Element, type:string, y:number, init:object={} )=>{
		target.dispatchEvent( new MouseEvent( type, {clientY: y, button: 0, bubbles: true, cancelable: true, ...init} ) );
		fixture.detectChanges();
	};
	const box = ( e:Element )=>[(<HTMLElement>e).style.top, (<HTMLElement>e).style.height];
	return { fixture, grid, root, set, opened, drawn, column, at, box };
};

describe( 'CalendarGrid', ()=>{
	it( "draws a column per day under its name, and the hours down from the day's start", ()=>{
		const d = create();
		expect( [...d.root.querySelectorAll( '.head .full' )].map( e=>e.textContent ) ).toEqual( ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'] );
		expect( [...d.root.querySelectorAll( '.head' )].map( e=>e.classList.contains( 'current' ) ) ).toEqual( [false, false, true, false, false, false, false] );//the chosen date's
		expect( d.root.querySelector( '.head .sub' ) ).toBeNull();
		expect( [...d.root.querySelectorAll( '.column' )].map( e=>e.getAttribute( 'aria-label' ) ) ).toEqual( ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'] );
		const hours = [...d.root.querySelectorAll<HTMLElement>( '.axis span' )];
		expect( hours ).toHaveLength( 24 );
		expect( [hours[0].textContent, hours[0].style.top, hours[18].textContent, hours[18].style.top] ).toEqual( ['06:00', '0px', '00:00', '504px'] );
		expect( d.column( 0 ).style.height ).toBe( '672px' );//24 hours of 28px
		expect( (<HTMLElement>d.fixture.nativeElement).style.getPropertyValue( '--columns' ) ).toBe( '7' );
	} );
	it( "heads a rotation's columns with the day's number and the date it falls on", ()=>{
		const d = create( {columns: patternColumns( {cycleDays: 14, cycleStart: new Date( '2026-07-01T10:00:00Z' )}, '2026-07-20', NewYork )} );
		const heads = [...d.root.querySelectorAll( '.head' )];
		expect( heads ).toHaveLength( 14 );
		expect( [heads[0].querySelector( '.full' )?.textContent, heads[0].querySelector( '.sub' )?.textContent] ).toEqual( ['Day 1', 'Wed 07-15'] );
		expect( heads[5].classList.contains( 'current' ) ).toBe( true );
		expect( d.column( 13 ).getAttribute( 'aria-label' ) ).toBe( 'Day 14, Tue 07-28' );
	} );
	//minutes to pixels:  28px an hour
	it( 'places a bar by its minutes, and cuts its breaks out of it', ()=>{
		const d = create( {bars: patternBars( [day, night], weekly, 360 )} );
		const bars = [...d.column( 0 ).querySelectorAll( '.bar' )];
		expect( bars.map( d.box ) ).toEqual( [['0px', '224px'], ['448px', '224px']] );//06:00 to 14:00, and 22:00 to the day's end
		expect( bars.map( b=>[b.querySelector( '.name' )?.textContent, b.querySelector( '.times' )?.textContent] ) ).toEqual( [['Day', '06:00 - 14:00'], ['Night', '22:00 - 06:00']] );
		expect( [...bars[0].querySelectorAll( '.cut' )].map( d.box ) ).toEqual( [['112px', '14px']] );//10:00, half an hour
		expect( bars[1].querySelector( '.cut' ) ).toBeNull();
		expect( d.root.querySelectorAll( '.bar' ) ).toHaveLength( 10 );
		expect( d.column( 5 ).querySelector( '.bar' ) ).toBeNull();//Saturday
		expect( bars.map( b=>[b.classList.contains( 'tone-1' ), b.classList.contains( 'tone-2' )] ) ).toEqual( [[true, false], [false, true]] );
		d.set( {hourHeight: 60} );
		expect( [...d.column( 0 ).querySelectorAll( '.bar' )].map( d.box ) ).toEqual( [['0px', '480px'], ['960px', '480px']] );
	} );
	it( "continues a shift past the day's end at the top of the next column", ()=>{
		const d = create( {bars: patternBars( [late], weekly, 360 )} );
		const sunday = d.column( 6 ).querySelector( '.bar' )!, monday = d.column( 0 ).querySelector( '.bar' )!;
		expect( [d.box( sunday ), sunday.classList.contains( 'continues' )] ).toEqual( [['560px', '112px'], true] );
		expect( [d.box( monday ), monday.classList.contains( 'continued' )] ).toEqual( [['0px', '112px'], true] );
	} );
	//not editable, as the Preview:  the picture alone
	it( 'says each bar to a screen reader where it is not editable, and takes no drag', ()=>{
		const d = create( {bars: patternBars( [day], weekly, 360 )} );
		expect( d.root.querySelector( 'button' ) ).toBeNull();
		const bar = d.column( 0 ).querySelector( '.bar' )!;
		expect( [bar.getAttribute( 'role' ), bar.getAttribute( 'aria-label' )] ).toEqual( ['img', 'Day, Monday, Tuesday, Wednesday, Thursday, Friday, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)'] );
		d.at( d.column( 5 ), 'pointerdown', 112 );
		expect( d.grid.drag() ).toBeUndefined();
		expect( d.column( 5 ).classList.contains( 'editable' ) ).toBe( false );
	} );
	it( "makes a shift's bars buttons that ask for its form, the first its one tab stop", ()=>{
		const d = create( {bars: patternBars( [day, late], weekly, 360 ), editable: true} );
		const bars = [...d.root.querySelectorAll<HTMLButtonElement>( 'button.bar' )];
		expect( bars ).toHaveLength( 7 );
		expect( bars.filter( b=>b.getAttribute( 'tabindex' )===null ).map( b=>b.getAttribute( 'aria-label' ) ) ).toEqual( [
			'Day, Monday, Tuesday, Wednesday, Thursday, Friday, 06:00 to 14:00, 8 h, breaks 10:00 (30 min)',
			'Late, Sunday, 02:00 to 10:00, 8 h'//Sunday's, not its continuation in Monday's column
		] );
		expect( bars.filter( b=>b.getAttribute( 'tabindex' )=='-1' ).every( b=>b.getAttribute( 'aria-hidden' )=='true' ) ).toBe( true );
		(<HTMLButtonElement>d.column( 2 ).querySelector( 'button.bar' )).click();//Wednesday's, a bar for the pointer
		expect( d.opened ).toHaveBeenCalledWith( 7 );
		(<HTMLButtonElement>d.column( 0 ).querySelector( 'button.bar.continued' )).click();//Late's continuation
		expect( d.opened ).toHaveBeenLastCalledWith( 9 );
		expect( d.drawn ).not.toHaveBeenCalled();
	} );
	//a drag on empty space creates a shift from the dragged hours:  240 minutes down is 10:00 under a 06:00 day start
	it( 'draws a shift over the hours dragged on a day, to the quarter hour', ()=>{
		const d = create( {editable: true} );
		const wednesday = d.column( 2 );
		d.at( wednesday, 'pointerdown', 112 );
		expect( d.grid.drag() ).toEqual( {column: 2, from: 240, to: 240} );
		expect( d.root.querySelector( '.drawing' ) ).toBeNull();//nothing dragged yet
		d.at( wednesday, 'pointermove', 340 );//728 minutes down:  the nearer quarter is 735
		const drawing = wednesday.querySelector( '.drawing' )!;
		expect( [d.box( drawing ), drawing.textContent] ).toEqual( [['112px', '231px'], '10:00 - 18:15'] );
		expect( d.column( 1 ).querySelector( '.drawing' ) ).toBeNull();
		d.at( wednesday, 'pointerup', 340 );
		expect( d.drawn ).toHaveBeenCalledWith( {day: 2, start: 240, minutes: 495} );
		expect( d.grid.drag() ).toBeUndefined();
		expect( d.root.querySelector( '.drawing' ) ).toBeNull();
	} );
	it( 'draws upwards too, and nothing for a click, a touch, Escape or a press on a bar', ()=>{
		const d = create( {bars: patternBars( [day], weekly, 360 ), editable: true} );
		const saturday = d.column( 5 );
		d.at( saturday, 'pointerdown', 448 );
		d.at( saturday, 'pointermove', 224 );
		d.at( saturday, 'pointerup', 224 );
		expect( d.drawn ).toHaveBeenLastCalledWith( {day: 5, start: 480, minutes: 480} );
		d.at( saturday, 'pointerdown', 448 );//a click
		d.at( saturday, 'pointerup', 448 );
		expect( d.drawn ).toHaveBeenCalledTimes( 1 );
		d.at( saturday, 'pointerdown', 448 );
		d.at( saturday, 'pointermove', 224 );
		document.dispatchEvent( new KeyboardEvent( 'keydown', {key: 'Escape'} ) );
		d.at( saturday, 'pointerup', 224 );
		expect( d.drawn ).toHaveBeenCalledTimes( 1 );
		const touch = new MouseEvent( 'pointerdown', {clientY: 448, button: 0, bubbles: true} );
		Object.defineProperty( touch, 'pointerType', {value: 'touch'} );//which scrolls the page
		saturday.dispatchEvent( touch );
		expect( d.grid.drag() ).toBeUndefined();
		d.at( d.column( 0 ).querySelector( '.bar' )!, 'pointerdown', 50 );//on Monday's Day
		expect( d.grid.drag() ).toBeUndefined();
		d.at( saturday, 'pointerdown', 448, {button: 2} );//the context menu's button
		expect( d.grid.drag() ).toBeUndefined();
	} );
	//the Preview's week:  an open exception's bar under its name, and a closed one's band behind what it left
	it( "draws a week's occurrences, an open exception's under its name, over the closed exceptions' bands", ()=>{
		const occurrence = ( shift:number|null, name:string, day:string, start:string, end:string )=>toCalendarOccurrence( {shift, name, day, start, end, breaks: []} );
		const bars = occurrenceBars( [
			occurrence( 7, 'Day', '2026-07-03', '2026-07-03T10:00:00Z', '2026-07-03T12:00:00Z' ),
			occurrence( 7, 'Day', '2026-07-03', '2026-07-03T14:00:00Z', '2026-07-03T18:00:00Z' ),
			occurrence( null, 'Extra Saturday', '2026-07-04', '2026-07-04T10:00:00Z', '2026-07-04T18:00:00Z' )
		], '2026-06-29', NewYork, 360 );
		const bands = exceptionBands( [toCalendarException( {id: 1, calendarId: 3, name: 'Maintenance', start: '2026-07-03T12:00:00Z', finish: '2026-07-03T14:00:00Z', isOpen: false} )], '2026-06-29', NewYork, 360 );
		const d = create( {columns: weekColumns( '2026-06-29', '2026-07-01' ), bars, bands} );
		expect( d.column( 4 ).getAttribute( 'aria-label' ) ).toBe( 'Friday, 2026-07-03' );
		expect( [...d.column( 4 ).querySelectorAll( '.bar' )].map( d.box ) ).toEqual( [['0px', '56px'], ['112px', '112px']] );//the shift, split
		const band = d.column( 4 ).querySelector( '.band' )!;
		expect( [d.box( band ), band.textContent?.trim()] ).toEqual( [['56px', '56px'], 'Maintenance'] );//between its halves
		const open = d.column( 5 ).querySelector( '.bar' )!;
		expect( [open.classList.contains( 'tone-3' ), open.querySelector( '.name' )?.textContent, open.getAttribute( 'aria-label' )] ).toEqual( [true, 'Extra Saturday', 'Extra Saturday, 2026-07-04, 06:00 to 14:00, 8 h, an open exception'] );
		expect( d.root.querySelector( '.head.current .sub' )?.textContent ).toBe( '2026-07-01' );//today
	} );
	it( 'notes the night the clocks change on its bar', ()=>{
		const bars = occurrenceBars( [toCalendarOccurrence( {shift: 8, name: 'Night', day: '2026-03-07', start: '2026-03-08T03:00:00Z', end: '2026-03-08T10:00:00Z', breaks: []} )], '2026-03-02', NewYork, 360 );
		const d = create( {columns: weekColumns( '2026-03-02' ), bars} );
		expect( d.column( 5 ).querySelector( '.bar .note' )?.textContent ).toBe( '7 h, clock change' );
	} );
} );