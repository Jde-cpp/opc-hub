import { AfterViewInit, Directive, ElementRef, OnDestroy, inject, input, output } from '@angular/core';
import { Paginator } from '../paginator/paginator';

//What a cell holds that takes the focus or a click:  out of the tab order while the grid has them, and reached through their cell.
const widgets = 'a[href], button, input, select, textarea, [tabindex]';
//What Enter presses:  a control, never what is focusable for its tooltip alone, a lock or a quality icon
const controls = 'a[href], button, input, select, textarea, [role=button], [role=checkbox], [role=switch], [role=link]';
const typed = 'input:not([type=checkbox]):not([type=radio]), textarea, select, [contenteditable]';
const moves = ["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Home", "End", "PageUp", "PageDown"];

//A table as a grid of the WAI-ARIA pattern (views spec *Keyboard*), on ql-table and the hand-built tables alike:  `role=grid`
//and one tab stop, the cell the arrows last reached, the header row its first row.  The keys act on what the mouse would:
//  - the arrows, Home and End, Ctrl+Home and Ctrl+End move between cells;  PageDown and PageUp turn `pager`'s pages, the focus
//    kept at its place, and move `pageRows` rows where the table has no pager;
//  - Enter is a click:  on the one control a cell holds, a header's menu button before its sort, else on the row's link, else on
//    the row, and on nothing where the cell's controls are disabled.  A cell with several takes the focus to its first, as F2
//    does on any;  Tab then goes between them, and Escape or F2 returns to the cell;
//  - Space ticks the row's checkbox, its `select` column's, and Shift+Up and Shift+Down tick as they go;  with no such column
//    Shift+Space is `rowSelect`, the row's place among the rows.
//The controls in the cells leave the tab order - Material's sort header among them, which gives its container `tabindex=0` -
//and a cell drawn again while the grid has the focus takes it back at the same place.
@Directive({
	selector: 'mat-table[keyGrid]',
	host: { class: 'jde-key-grid', '(keydown)': 'onKey($event)', '(focusin)': 'onFocusIn($event)', '(focusout)': 'onFocusOut($event)' }
})
export class KeyGrid implements AfterViewInit, OnDestroy{
	#el = inject<ElementRef<HTMLElement>>( ElementRef ).nativeElement;
	constructor(){
		this.#el.setAttribute( "role", "grid" );//after CdkTable's own `table`, and before its first cell asks for its role
	}
	ngAfterViewInit(){
		this.#sweep( [] );
		this.#observer = new MutationObserver( records=>this.#sweep(records) );
		this.#observer.observe( this.#el, {childList: true, subtree: true, attributes: true, attributeFilter: ["tabindex"]} );
	}
	ngOnDestroy(){ this.#observer?.disconnect(); }

	pager = input<Paginator>();
	rowSelect = output<number>();
	static readonly pageRows = 10;

	onKey( event:KeyboardEvent ){
		const target = <HTMLElement>event.target, cell = this.#cellOf( target );
		if( !cell || event.altKey )
			return;
		if( target!==cell && !( moves.includes(event.key) && !target.matches(typed) ) ){
			this.#onWidgetKey( event, cell, target );
			return;
		}
		const rows = this.#rows(), row = rows.indexOf( cell.parentElement! ), last = rows[row].children.length-1;
		const col = [...rows[row].children].indexOf( cell ), ctrl = event.ctrlKey || event.metaKey;
		switch( event.key ){
		case "ArrowRight": this.#go( rows, row, Math.min(col+1, last), true ); break;
		case "ArrowLeft": this.#go( rows, row, Math.max(col-1, 0), true ); break;
		case "ArrowDown": case "ArrowUp":{
			const by = event.key=="ArrowDown" ? 1 : -1;
			if( event.shiftKey )
				[rows[row], rows[row+by]].forEach( r=>this.#tick(r, true) );
			this.#go( rows, row+by, this.#col );
			break;
		}
		case "Home": this.#go( rows, ctrl ? 0 : row, 0, true ); break;
		case "End": this.#go( rows, ctrl ? rows.length-1 : row, Infinity, true ); break;
		case "PageDown": case "PageUp":{
			const by = event.key=="PageDown" ? 1 : -1, pager = this.pager();
			if( pager ){
				if( !event.repeat )//a held key turns one page:  repeats outrun the reads, and a list of no known length ran on past its end
					pager.turn( by );
			}
			else
				this.#go( rows, row+by*KeyGrid.pageRows, this.#col );
			break;
		}
		case "Enter": this.#enter( cell ); break;
		case "F2": this.#widgets( cell )[0]?.focus(); break;
		case " ":
			if( !this.#tick(rows[row]) && event.shiftKey && this.#isDataRow(rows[row]) )
				this.rowSelect.emit( rows.filter( r=>this.#isDataRow(r) ).indexOf(rows[row]) );
			break;
		default:
			return;
		}
		event.preventDefault();
	}
	//a control in a cell has the focus:  its keys are its own, but those that leave it
	#onWidgetKey( event:KeyboardEvent, cell:HTMLElement, target:HTMLElement ){
		if( event.key=="Escape" || event.key=="F2" ){
			event.preventDefault();
			event.stopPropagation();//not a dialog's close as well
			cell.focus();
		}
		else if( event.key=="Tab" ){
			const all = this.#widgets( cell ), next = all[all.indexOf(target)+( event.shiftKey ? -1 : 1 )];
			if( next ){
				event.preventDefault();
				next.focus();
			}
		}
	}
	//a cell given the focus by its own component, drawn this moment:  a header's, after its menu moved it
	focusCell( cell:HTMLElement ){
		this.#activate( cell );
		cell.focus();
	}
	onFocusIn( event:FocusEvent ){
		this.#within = true;
		const target = <HTMLElement>event.target, cell = this.#cellOf( target );
		if( !cell )
			return;
		const back = this.#returnTo===cell && target!==cell;//a menu the cell's Enter opened, closed:  the trigger has the focus
		this.#returnTo = undefined;
		if( back )
			cell.focus();
		else
			this.#activate( cell );
	}
	onFocusOut( event:FocusEvent ){
		const to = <Node|null>event.relatedTarget;
		if( to ){
			this.#within = this.#el.contains( to );
			return;
		}
		//To nowhere:  a click on the page, or the cell taken out of it, which #sweep puts the focus back for.  Chrome says so before
		//the cell is out, so which it was is read once the removal is done.
		this.#left = <Node>event.target;
		queueMicrotask( ()=>{
			if( !this.#left )
				return;//a sweep read it
			const records = this.#observer?.takeRecords() ?? [];//the removal's, ahead of the observer's own callback
			this.#settle( records );
			if( records.length )
				this.#sweep( records );
		} );
	}
	//A node in the page is a click on the page, unless the records took it out:  a row CdkTable moved, taken out and put back in one
	//task (reviews-issue/597-views-keyboard.md #1)
	#settle( records:readonly MutationRecord[] ){
		const left = this.#left;
		this.#left = undefined;
		if( left?.isConnected && !records.some(r=>[...r.removedNodes].some(n=>n.contains(left))) )
			this.#within = false;
	}

	#enter( cell:HTMLElement ){
		const row = cell.parentElement!, inside = this.#widgets( cell ), pressed = inside.filter( w=>w.matches(controls) );
		const menu = this.#isDataRow( row ) ? undefined : pressed.find( w=>w.hasAttribute("aria-haspopup") );
		const one = menu ?? ( pressed.length==1 ? pressed[0] : undefined );
		if( one ){
			if( one.hasAttribute("aria-haspopup") )
				this.#returnTo = cell;
			one.click();
		}
		else if( pressed.length )
			inside[0].focus();
		else if( this.#widgets(cell, true).some(w=>w.matches(controls)) )
			return;//every control disabled, which a click on would leave be
		else if( this.#isDataRow(row) )
			( row.querySelector<HTMLElement>( "a[href]" ) ?? row ).click();
	}
	//the row's checkbox clicked, or with `on` only where it is off:  false where the row has none
	#tick( row:Element|undefined, on=false ):boolean{
		const box = row?.querySelector<HTMLInputElement>( ".mat-column-select input[type=checkbox]" );
		if( !box || ( on && !this.#isDataRow(row!) ) )
			return false;
		if( !box.disabled && !( on && box.checked ) )
			box.click();
		return true;
	}
	//`col` is kept through a row with fewer cells, a group's header:  `set`, a move along the row, makes it the column wanted
	#go( rows:HTMLElement[], row:number, col:number, set=false ){
		const cells = rows[Math.max( 0, Math.min(row, rows.length-1) )]?.children;
		const cell = <HTMLElement|undefined>cells?.[Math.min( col, cells.length-1 )];
		if( !cell )
			return;
		if( set )
			this.#col = Math.min( col, cells!.length-1 );
		this.#activate( cell, this.#col );
		cell.focus();
	}
	#activate( cell:HTMLElement, col?:number ){
		if( this.#sweeping )
			return;
		this.#displaced = false;
		this.#row = this.#rows().indexOf( cell.parentElement! );
		if( cell===this.#active )
			return;
		this.#col = col ?? [...cell.parentElement!.children].indexOf( cell );
		this.#active?.setAttribute( "tabindex", "-1" );
		cell.setAttribute( "tabindex", "0" );
		this.#active = cell;
	}
	//One tab stop:  the active cell, or the one at its place where the table drew it again, which takes the focus the grid had.
	//A place past the last row is kept, for rows a page turn has yet to bring.
	#sweep( records:readonly MutationRecord[] ){
		this.#settle( records );
		const rows = this.#rows();
		const was = this.#active, had = !!was && document.activeElement===was;
		let active = was;
		if( !active || !this.#el.contains(active) || this.#displaced ){
			const row = Math.min( this.#row, rows.length-1 ), cells = rows[row]?.children;
			active = <HTMLElement|undefined>cells?.[Math.min( this.#col, cells.length-1 )];
			this.#displaced = row!=this.#row;
		}
		else
			this.#row = rows.indexOf( active.parentElement! );
		for( const row of rows ){
			for( const cell of row.children ){
				const index = cell===active ? "0" : "-1";
				if( cell.getAttribute("tabindex")!=index )
					cell.setAttribute( "tabindex", index );
			}
		}
		for( const widget of this.#el.querySelectorAll(widgets) ){
			if( widget.getAttribute("tabindex")!="-1" && !this.#isRow(widget.parentElement) )
				widget.setAttribute( "tabindex", "-1" );
		}
		this.#active = active;
		if( active && ( had ? active!==was : this.#within && !this.#el.contains(document.activeElement) ) ){
			this.#sweeping = true;
			active.focus();
			this.#sweeping = false;
		}
	}
	#rows():HTMLElement[]{ return ( <HTMLElement[]>[...this.#el.children] ).filter( e=>this.#isRow(e) ); }
	#isRow( e:Element|null ):boolean{ return !!e && e.parentElement===this.#el && ( e.classList.contains("mat-mdc-header-row") || e.classList.contains("mat-mdc-row") ); }
	#isDataRow( e:Element ):boolean{ return e.classList.contains( "mat-mdc-row" ); }
	#cellOf( target:HTMLElement|null ):HTMLElement|undefined{
		let e = target;
		while( e && !this.#isRow(e.parentElement) )
			e = e===this.#el ? null : e.parentElement;
		return e ?? undefined;
	}
	#widgets( cell:HTMLElement, disabled=false ):HTMLElement[]{
		return [...cell.querySelectorAll<HTMLElement>( widgets )].filter( w=>!w.closest("[aria-hidden=true]") && ( disabled || !( <HTMLButtonElement>w ).disabled && w.getAttribute("aria-disabled")!="true" ) );
	}
	#observer:MutationObserver|undefined;
	#active:HTMLElement|undefined;
	#row = 0;
	#col = 0;
	#within = false;//the focus is in the grid, or was in a cell since taken out of the page
	#left:Node|undefined;//what the focus left for nowhere, until #settle
	#displaced = false;//the active cell stands in for one of a row not drawn yet
	#sweeping = false;
	#returnTo:HTMLElement|undefined;
}