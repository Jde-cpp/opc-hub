import { TestBed } from '@angular/core/testing';
import { Paginator, PageEvent } from './paginator';

//231-paging #1:  the handlers sat on the <mat-icon> inside each button.  Enter/Space on a focused button dispatch the
//click to the button itself, so a keyboard user could not page - clicking the button element is that path.
describe( 'Paginator buttons', ()=>{
	const create = ()=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( Paginator );
		fixture.componentRef.setInput( 'pageSize', 25 );
		fixture.componentRef.setInput( 'startIndex', 25 );
		fixture.componentRef.setInput( 'length', 100 );
		fixture.detectChanges();
		const emitted:PageEvent[] = [];
		fixture.componentInstance.onPageEvent.subscribe( e=>emitted.push(e) );
		return { fixture, emitted };
	};
	const button = ( fixture:ReturnType<typeof create>['fixture'], label:string )=>
		fixture.nativeElement.querySelector( `button[aria-label="${label}"]` ) as HTMLButtonElement;

	it.each( [
		['First page', 0],
		['Previous page', 0],
		['Previous row', 24],
		['Next row', 26],
		['Next page', 50],
		['Last page', 75]
	] )( '%s pages from a click on the button', ( label, startIndex )=>{
		const { fixture, emitted } = create();
		button( fixture, label ).click();
		expect( emitted.map(e=>e.startIndex) ).toEqual( [startIndex] );
	} );

	it( 'offers Last only with a total, and First regardless', ()=>{
		const { fixture } = create();
		fixture.componentRef.setInput( 'length', undefined );
		fixture.detectChanges();
		expect( button( fixture, 'Last page' ) ).toBeNull();
		expect( button( fixture, 'First page' ) ).not.toBeNull();
	} );

	it( 'announces the range', ()=>{
		const { fixture } = create();
		const range = fixture.nativeElement.querySelector( '.mat-mdc-paginator-range-label' ) as HTMLElement;
		expect( range.getAttribute('aria-live') ).toBe( 'polite' );
		button( fixture, 'Next page' ).click();
		fixture.detectChanges();
		expect( range.textContent!.trim() ).toBe( '51 – 75 of 100' );
	} );
} );

//reviews-issue/329-sticky-table-header.md #6:  the pinned pager turns pages from part-way down a list
describe( 'Paginator.showFirstRow', ()=>{
	//a list with a header row and two rows, the first one's scrollIntoView recorded (jsdom has none)
	const list = ()=>{
		const host = document.createElement( 'div' );
		host.innerHTML = '<mat-header-row></mat-header-row><mat-row></mat-row><mat-row></mat-row>';
		const first = host.querySelector( 'mat-row' ) as HTMLElement;
		first.scrollIntoView = vi.fn();
		return { host, scroll: first.scrollIntoView as ReturnType<typeof vi.fn> };
	};

	it( 'brings the first row into view on a page turn either way', ()=>{
		const { host, scroll } = list();
		Paginator.showFirstRow( host, 0, 25 );
		Paginator.showFirstRow( host, 25, 0 );
		expect( scroll.mock.calls ).toEqual( [[{block: 'nearest'}], [{block: 'nearest'}]] );
	} );

	it( 'keeps the scroll on a one-row step', ()=>{
		const { host, scroll } = list();
		Paginator.showFirstRow( host, 25, 26 );
		Paginator.showFirstRow( host, 25, 24 );
		expect( scroll ).not.toHaveBeenCalled();
	} );

	it( 'leaves a list with no rows alone', ()=>{
		expect( ()=>Paginator.showFirstRow( document.createElement('div'), 0, 25 ) ).not.toThrow();
	} );
} );