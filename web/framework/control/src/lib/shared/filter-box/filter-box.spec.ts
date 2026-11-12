import { TestBed } from '@angular/core/testing';
import { Field } from '../../model/ql/schema/field';
import { FilterColumn, FilterHost } from '../../model/ql/filter-query';
import { FilterBox } from './filter-box';
import { flush } from 'jde-spa/testing';

//#591:  the filter box - the text read on every keystroke, a term it cannot read underlined and named, the list asked for again
//on Enter, on a suggestion taken and 400 ms after the last keystroke.
const column = ( name:string, type:object, displayName:string ):FilterColumn=>({ qlField: new Field({name, type} as Partial<Field>), displayName });
const host:FilterHost = { server: true, enums: new Map( [["Provider", [{name: "Google"}, {name: "Open Id"}]]] ), columns: [
	column( "name", {kind: "SCALAR", name: "String"}, "Name" ),
	column( "provider", {kind: "ENUM", name: "Provider"}, "Provider" )
] };

describe( 'FilterBox', ()=>{
	afterEach( ()=>vi.useRealTimers() );
	//A box on the view's `filter`.  sent:  the texts it asked the list for.  type:  the text typed, the cursor at its end.
	const create = ( filter="" )=>{
		TestBed.configureTestingModule( {} );
		const fixture = TestBed.createComponent( FilterBox );
		fixture.componentRef.setInput( 'filter', filter );
		fixture.componentRef.setInput( 'host', host );
		fixture.componentRef.setInput( 'values', {name: ["Pump", "pump station"]} );
		const sent = vi.fn();
		fixture.componentInstance.filterChange.subscribe( sent );
		fixture.detectChanges();
		const element = fixture.nativeElement as HTMLElement, input = element.querySelector( 'input' )!;
		const type = ( text:string )=>{
			input.focus();
			input.value = text;
			input.setSelectionRange( text.length, text.length );
			input.dispatchEvent( new Event('input', {bubbles: true}) );
			fixture.detectChanges();
			fixture.detectChanges();//the field, once the input has said it holds a term that is not read
		};
		const enter = ()=>{
			input.dispatchEvent( new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}) );
			fixture.detectChanges();
		};
		return { fixture, element, input, sent, type, enter };
	};
	const options = ()=>[...document.querySelectorAll<HTMLElement>( 'mat-option' )];
	const take = async ( label:string )=>{
		options().find( o=>o.textContent!.trim()==label )!.click();
		await flush();
	};

	it( "shows the view's text, and another view's as it comes, asking for nothing", ()=>{
		const { fixture, input, sent } = create( "name:a" );
		expect( input.value ).toBe( "name:a" );
		fixture.componentRef.setInput( 'filter', "provider:Google" );
		fixture.detectChanges();
		expect( input.value ).toBe( "provider:Google" );
		expect( sent ).not.toHaveBeenCalled();
	} );

	it( 'underlines a term it cannot read, names it under the box, and reads the rest', ()=>{
		const { fixture, element, input, type } = create();
		type( "nope:1 name:a provider:x" );
		expect( [...element.querySelectorAll( '.marks .bad' )].map( b=>b.textContent ) ).toEqual( ["nope:1", "provider:x"] );
		expect( element.querySelector( '.marks' )!.textContent ).toBe( "nope:1 name:a provider:x" );
		expect( element.querySelector( 'mat-error' )!.textContent!.trim() ).toBe( "'x' is not a value of 'Provider' - Google, Open Id.  1 more not read." );//the term the cursor is in
		expect( input.getAttribute( "aria-invalid" ) ).toBe( "true" );
		expect( fixture.componentInstance.parsed().terms.map( t=>t.field ) ).toEqual( ["name"] );
		type( "name:a" );
		expect( element.querySelector( '.marks .bad' ) ).toBeNull();
		expect( element.querySelector( 'mat-error' ) ).toBeNull();
		expect( input.getAttribute( "aria-invalid" ) ).toBe( "false" );
	} );

	it( 'asks 400 ms after the last keystroke, once, and not for the text the view holds', ()=>{
		vi.useFakeTimers();
		const { sent, type } = create( "name:a" );
		type( "name:a p" );
		vi.advanceTimersByTime( 399 );
		type( "name:a pu" );
		vi.advanceTimersByTime( 399 );
		expect( sent ).not.toHaveBeenCalled();
		vi.advanceTimersByTime( 1 );
		expect( sent.mock.calls ).toEqual( [["name:a pu"]] );
		type( "name:a pu " );//a space more is the same filter
		vi.advanceTimersByTime( 400 );
		type( "name:a" );
		type( "name:a pu" );
		vi.advanceTimersByTime( 400 );
		expect( sent ).toHaveBeenCalledTimes( 1 );
	} );

	//reviews-issue/591-views-filter-box.md #4:  the view's echo of what the box sent put the text back over a key typed since
	it( "keeps a key typed after it asks, ahead of the view's echo", ()=>{
		vi.useFakeTimers();
		const { fixture, input, sent, type } = create();
		type( "name:pu" );
		vi.advanceTimersByTime( 400 );
		type( "name:pum" );
		fixture.componentRef.setInput( 'filter', "name:pu" );
		fixture.detectChanges();
		expect( input.value ).toBe( "name:pum" );
		vi.advanceTimersByTime( 400 );
		expect( sent.mock.calls ).toEqual( [["name:pu"], ["name:pum"]] );
	} );

	//reviews-issue/591-views-filter-box.md #12:  the copy behind the input kept the scroll of the text it replaced
	it( "draws the underline under a text put in place of a scrolled one", async ()=>{
		const { fixture, element } = create( "nope:1 " + "name:a ".repeat(40) );
		fixture.componentInstance.scroll.set( 300 );//scrolled to its end
		fixture.detectChanges();
		fixture.componentRef.setInput( 'filter', "nope:2" );
		fixture.detectChanges();
		await flush();
		fixture.detectChanges();
		expect( fixture.componentInstance.scroll() ).toBe( 0 );
		expect( element.querySelector<HTMLElement>( '.marks .run' )!.style.transform ).toBe( "translateX(0px)" );
	} );

	//reviews-issue/591-views-filter-box.md #5:  Save and Discard, before the keystroke comes due
	it( "asks at once for what is typed on flush, and drops it on revert", ()=>{
		vi.useFakeTimers();
		const { fixture, input, sent, type } = create( "name:a" );
		type( "name:ab" );
		fixture.componentInstance.flush();
		expect( sent.mock.calls ).toEqual( [["name:ab"]] );
		type( "name:abc" );
		fixture.componentInstance.revert();
		fixture.detectChanges();
		expect( input.value ).toBe( "name:a" );
		vi.advanceTimersByTime( 400 );
		expect( sent ).toHaveBeenCalledTimes( 1 );
		fixture.componentInstance.flush();//nothing typed:  nothing asked for
		expect( sent ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'asks on Enter at once, and not again when the keystroke comes due', ()=>{
		vi.useFakeTimers();
		const { sent, type, enter } = create();
		type( "pump" );
		enter();
		expect( sent.mock.calls ).toEqual( [["pump"]] );
		vi.advanceTimersByTime( 400 );
		expect( sent ).toHaveBeenCalledTimes( 1 );
	} );

	it( "offers the headers, writes one taken with its colon and offers its values, without asking", async ()=>{
		const { fixture, input, sent, type } = create();
		type( "pro" );
		expect( options().map( o=>o.textContent!.trim() ) ).toEqual( ["Provider:"] );
		await take( "Provider:" );
		fixture.detectChanges();
		await flush();
		fixture.detectChanges();
		expect( input.value ).toBe( "Provider:" );
		expect( sent ).not.toHaveBeenCalled();
		expect( options().map( o=>o.textContent!.trim() ) ).toEqual( ["Google", "Open Id"] );
	} );

	it( 'quotes a suggestion with a space as it writes it, and asks for the list', async ()=>{
		const { fixture, input, sent, type } = create();
		type( "provider:op" );
		await take( "Open Id" );
		fixture.detectChanges();
		expect( input.value ).toBe( 'provider:"Open Id"' );
		expect( input.selectionStart ).toBe( 18 );
		expect( sent.mock.calls ).toEqual( [['provider:"Open Id"']] );
		type( "name:" );
		await take( "pump station" );
		expect( input.value ).toBe( 'name:"pump station"' );
	} );

	//#597
	it( 'takes the focus on Ctrl+\\ and Cmd+\\ from anywhere on the page, its text selected, and on no other key', ()=>{
		const { input } = create( "pump" );
		const press = ( init:KeyboardEventInit )=>{
			input.blur();
			const taken = !document.body.dispatchEvent( new KeyboardEvent('keydown', {bubbles: true, cancelable: true, ...init}) );
			return [taken, document.activeElement===input];
		};
		expect( press( {key: "\\", ctrlKey: true} ) ).toEqual( [true, true] );
		expect( [input.selectionStart, input.selectionEnd] ).toEqual( [0, 4] );
		expect( press( {key: "\\", metaKey: true} ) ).toEqual( [true, true] );
		expect( press( {key: "#", code: "Backslash", ctrlKey: true} ) ).toEqual( [true, true] );//a layout with another character on the key
		expect( press( {key: "\\"} ) ).toEqual( [false, false] );
		expect( press( {key: "\\", ctrlKey: true, altKey: true} ) ).toEqual( [false, false] );
		expect( press( {key: "/", ctrlKey: true} ) ).toEqual( [false, false] );
	} );

	it( 'closes its suggestions on Escape, and with none open clears the box and asks', async ()=>{
		const { fixture, input, sent, type } = create( "pump" );
		const escape = async ()=>{
			input.dispatchEvent( new KeyboardEvent('keydown', {key: 'Escape', keyCode: 27, bubbles: true}) );
			await flush();
			fixture.detectChanges();
		};
		type( "pump s" );
		await flush();
		expect( options().length ).toBeGreaterThan( 0 );
		await escape();
		expect( [fixture.componentInstance.trigger().panelOpen, input.value, sent.mock.calls] ).toEqual( [false, "pump s", []] );
		await escape();
		expect( [input.value, sent.mock.calls] ).toEqual( ["", [[""]]] );
		await escape();
		expect( sent ).toHaveBeenCalledTimes( 1 );
	} );
} );