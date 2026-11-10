import { AfterViewInit, Component, Directive, ElementRef, OnDestroy, computed, effect, inject, input, output, viewChild } from '@angular/core';
import { CdkColumnDef } from '@angular/cdk/table';
import { CdkDrag, CdkDragDrop, CdkDropList } from '@angular/cdk/drag-drop';
import { MatIcon } from '@angular/material/icon';
import { MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger } from '@angular/material/menu';
import { MatSortHeader } from '@angular/material/sort';
import { View, ViewField } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';

//A table's columns as its view has them (views spec *Fields*), the same over ql-table and the hand-built tables of the logs, a
//node's children and the watch list:  on the mat-table, with `viewColumn` on each header cell and cell of a column the view holds.
//A header then has its menu and its resize handle and drags along the header row, each a change to the view's draft, after which
//`columnsChange` tells the host the view on screen is another, as the toolbar's `viewChange` does.  The header cells are the drop
//list's items wherever the table puts them:  a cell's injector is the column's, not the header row's, so the list is the table.
@Directive({
	selector: 'mat-table[viewColumns]',
	hostDirectives: [CdkDropList]
})
export class ViewColumns{
	constructor(){
		const list = inject( CdkDropList, {self: true} );
		list.orientation = "horizontal";
		list.lockAxis = "x";
		list.dropped.subscribe( ( event:CdkDragDrop<unknown> )=>this.move( event.item.data, event.currentIndex ) );
	}
	views = input<ViewSet<View>|undefined>( undefined, {alias: "viewColumns"} );//none:  the headers sort, and nothing else
	columnsChange = output<void>();
	view = computed<View|undefined>( ()=>this.views()?.current() );
	shown = computed<ViewField[]>( ()=>this.view()?.fields.filter( f=>f.displayed ) ?? [] );
	#fields = computed( ()=>new Map( this.view()?.fields.map( f=>[f.name, f] ) ) );
	field( name:string ):ViewField|undefined{ return this.#fields().get( name ); }
	place( name:string ):number{ return this.shown().findIndex( f=>f.name==name ); }

	hide( name:string ){ this.#edit( view=>view.setDisplayed(name, false) ); }
	move( name:string, to:number ){
		if( to!=this.place(name) )
			this.#edit( view=>view.moveField(name, to) );
	}
	width( name:string, width:number ){
		const to = `0 0 ${Math.max( View.minWidth, Math.round(width) )}px`;
		if( this.field(name)?.flex!=to )
			this.#edit( view=>view.setWidth(name, width) );
	}
	//the route's width, which on a hand-built table is its sheet's
	restoreWidth( name:string ){ this.#edit( view=>view.restoreWidth(name, this.views()?.routeDefault) ); }
	#edit( change:( view:View )=>void ){
		this.views()?.edit( change );
		this.columnsChange.emit();
	}
}

//A column's cell:  the width its view gives it, where it gives one, over any minimum its sheet has - else the sheet's, or ql-table's
//own binding - and pinned where the column is (View.pinned).
@Directive({
	selector: 'mat-cell[viewColumn]',
	host: { '[style.flex]': 'columns.field(name)?.flex', '[style.min-width]': 'columns.field(name)?.flex ? 0 : null', '[class.jde-pinned]': 'columns.view()?.pinned==name' }
})
export class ViewColumnCell{
	columns = inject( ViewColumns );
	#column = inject( CdkColumnDef );
	get name():string{ return this.#column.name; }
}

//A column's header cell.  The sort header is a span around the label, not the cell:  Material's sorts on any click within it and
//puts what it is given inside a button, so the menu button and the handle are beside it, and the cell says `aria-sort` itself.
//The menu button shows on hover and focus;  the handle is the cell's right edge, dragged for a width, double-clicked for the
//route's.  A press on either is kept from the header's drag.
@Component({
	selector: 'mat-header-cell[viewColumn]',
	templateUrl: './view-column-header.html',
	styleUrl: './view-column-header.scss',
	hostDirectives: [CdkDrag],
	imports: [MatIcon, MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger, MatSortHeader],
	host: { class: 'jde-column-header', '[style.flex]': 'field()?.flex', '[style.min-width]': 'field()?.flex ? 0 : null', '[class.jde-pinned]': 'columns.view()?.pinned==name', '[attr.aria-sort]': 'ariaSort()' }
})
export class ViewColumnHeader implements AfterViewInit, OnDestroy{
	columns = inject( ViewColumns );
	#column = inject( CdkColumnDef );
	#drag = inject( CdkDrag, {self: true} );
	#el = inject<ElementRef<HTMLElement>>( ElementRef );
	constructor(){
		this.#drag.data = this.name;
		this.#drag.lockAxis = "x";
		effect( ()=>{ this.#drag.disabled = !this.field(); } );//no drag where the table has no view, or the view no such column
	}
	//Material's sort header says aria-sort on its host, the span, where it is no attribute of the role:  the cell's is the one read
	ngAfterViewInit(){
		const label = this.label()?.nativeElement;
		if( !label )
			return;
		const scrub = ()=>label.removeAttribute( "aria-sort" );
		this.#scrubber = new MutationObserver( scrub );
		this.#scrubber.observe( label, {attributes: true, attributeFilter: ["aria-sort"]} );
		scrub();
	}
	ngOnDestroy(){ this.#scrubber?.disconnect(); }
	#scrubber:MutationObserver|undefined;
	get name():string{ return this.#column.name; }
	sortable = input<boolean>( true );
	label = viewChild( 'label', {read: ElementRef<HTMLElement>} );//its element:  the reference alone is the MatSortHeader
	field = computed<ViewField|undefined>( ()=>this.columns.field(this.name) );
	title = computed<string>( ()=>this.field()?.displayName ?? this.name );
	place = computed<number>( ()=>this.columns.place(this.name) );
	count = computed<number>( ()=>this.columns.shown().length );
	ariaSort = computed<string|null>( ()=>{
		if( !this.sortable() )
			return null;
		const sort = this.columns.view()?.sort[0];
		return sort?.active==this.name && sort.direction ? ( sort.direction=="asc" ? "ascending" : "descending" ) : "none";
	} );
	//kept from the cell:  a press would start its drag
	stop( event:Event ){ event.stopPropagation(); }

	//The width now:  as drawn, or - nothing laid out - as the view has it
	#width():number{
		return this.#el.nativeElement.getBoundingClientRect().width || parseFloat( this.field()?.flex?.split( " " )[2] ?? "" ) || 0;
	}
	static readonly step = 32;
	wider(){ this.columns.width( this.name, this.#width()+ViewColumnHeader.step ); }
	narrower(){ this.columns.width( this.name, this.#width()-ViewColumnHeader.step ); }
	get atFloor():boolean{ return this.field()?.flex==`0 0 ${View.minWidth}px`; }

	//the handle's drag:  the pointer is captured, so the moves are the handle's wherever the pointer goes
	#resize:{x:number, width:number}|undefined;
	onResizeStart( event:PointerEvent ){
		if( event.button )
			return;
		event.preventDefault();
		event.stopPropagation();
		( <HTMLElement>event.target ).setPointerCapture?.( event.pointerId );
		this.#resize = { x: event.clientX, width: this.#width() };
	}
	onResizeMove( event:PointerEvent ){
		if( this.#resize )
			this.columns.width( this.name, this.#resize.width+event.clientX-this.#resize.x );
	}
	onResizeEnd(){ this.#resize = undefined; }
	onResizeReset(){
		this.#resize = undefined;
		this.columns.restoreWidth( this.name );
	}
}