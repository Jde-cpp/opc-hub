import { AfterViewInit, Component, Directive, ElementRef, Injector, OnDestroy, afterNextRender, computed, effect, inject, input, output, signal, viewChild } from '@angular/core';
import { CdkColumnDef } from '@angular/cdk/table';
import { CdkDrag, CdkDragDrop, CdkDropList } from '@angular/cdk/drag-drop';
import { MatIcon } from '@angular/material/icon';
import { MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger } from '@angular/material/menu';
import { MatSortHeader } from '@angular/material/sort';
import { Flex, View, ViewField } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';

//A table's columns as its view has them (views spec *Fields*), the same over ql-table and the hand-built tables of the logs, a
//node's children and the watch list:  on the mat-table, with `viewColumn` on each header cell and cell of a column the view holds.
//A header then has its menu and its resize handle and drags along the header row, each a change to the view's draft, after which
//`columnsChange` tells the host the view on screen is another, as the toolbar's `viewChange` does.  The header cells are the drop
//list's items wherever the table puts them:  a cell's injector is the column's, not the header row's, so the list is the table.
//`jde-view-columns` makes the rows as wide as their columns (_general.scss), the block a pinned cell sticks in.
@Directive({
	selector: 'mat-table[viewColumns]',
	hostDirectives: [CdkDropList],
	host: { class: 'jde-view-columns' }
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
	pinned = computed<string|undefined>( ()=>this.view()?.pinned );
	#fields = computed( ()=>new Map( this.view()?.fields.map( f=>[f.name, f] ) ) );
	#flexes = computed( ()=>new Map( this.view()?.fields.map( f=>[f.name, f.flex] ) ) );
	field( name:string ):ViewField|undefined{ return this.#fields().get( name ); }
	place( name:string ):number{ return this.shown().findIndex( f=>f.name==name ); }
	//A column's width as drawn:  the one a handle's drag has reached, else the view's
	flex( name:string ):string|undefined{
		const preview = this.#preview();
		return preview?.name==name ? View.flexOf( preview.width ) : this.#flexes().get( name );
	}
	#preview = signal<{name:string, width:number}|undefined>( undefined );
	preview( name:string, width?:number ){ this.#preview.set( width==null ? undefined : {name, width} ); }

	//`focus`:  from a header's menu, whose button the header row drawn again destroys - the column moved takes the focus, or after
	//a hide its neighbour
	hide( name:string, focus=false ){
		const shown = this.shown(), at = this.place( name );
		if( focus )
			this.#focus = ( shown[at+1] ?? shown[at-1] )?.name;
		this.#edit( view=>view.setDisplayed(name, false) );
	}
	move( name:string, to:number, focus=false ){
		if( to==this.place(name) )
			return;
		if( focus )
			this.#focus = name;
		this.#edit( view=>view.moveField(name, to) );
	}
	//a header's, once it is drawn again
	takesFocus( name:string ):boolean{
		if( this.#focus!=name )
			return false;
		this.#focus = undefined;
		return true;
	}
	#focus:string|undefined;
	//`width` from `from`, the column's as drawn:  nothing where that leaves it as it was
	width( name:string, width:number, from:number ){
		if( View.widthOf(width, from)!=Math.round(from) )
			this.#edit( view=>view.setWidth(name, width, from) );
	}
	//the route's width, which on a hand-built table is its sheet's
	restoreWidth( name:string ){ this.#edit( view=>view.restoreWidth(name, this.views()?.routeDefault) ); }
	#edit( change:( view:View )=>void ){
		this.views()?.edit( change );
		this.columnsChange.emit();
	}
}

//A column's cell:  the width its view gives it, where it gives one, over any minimum its sheet has - else the sheet's - and pinned
//where the column is (View.pinned).
@Directive({
	selector: 'mat-cell[viewColumn]',
	host: { '[style.flex]': 'columns.flex(name)', '[style.min-width]': 'columns.flex(name) ? 0 : null', '[class.jde-pinned]': 'columns.pinned()==name' }
})
export class ViewColumnCell{
	columns = inject( ViewColumns );
	#column = inject( CdkColumnDef );
	get name():string{ return this.#column.name; }
}

//A column's header cell.  The sort header is a span around the label, not the cell:  Material's sorts on any click within it and
//puts what it is given inside a button, so the menu button and the handle are beside it, and the cell says `aria-sort` itself.
//The menu button shows on hover and focus;  the handle is the cell's right edge, dragged for a width, double-clicked for the
//route's.  A press on either is kept from the header's drag, and a touch drags the header only after a press and hold, so a
//swipe that starts on the header row scrolls.
@Component({
	selector: 'mat-header-cell[viewColumn]',
	templateUrl: './view-column-header.html',
	styleUrl: './view-column-header.scss',
	hostDirectives: [CdkDrag],
	imports: [MatIcon, MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger, MatSortHeader],
	host: { class: 'jde-column-header', '[style.flex]': 'columns.flex(name)', '[style.min-width]': 'columns.flex(name) ? 0 : null', '[class.jde-pinned]': 'columns.pinned()==name', '[attr.aria-sort]': 'ariaSort()' }
})
export class ViewColumnHeader implements AfterViewInit, OnDestroy{
	columns = inject( ViewColumns );
	#column = inject( CdkColumnDef );
	#drag = inject( CdkDrag, {self: true} );
	#el = inject<ElementRef<HTMLElement>>( ElementRef );
	#injector = inject( Injector );
	constructor(){
		this.#drag.data = this.name;
		this.#drag.lockAxis = "x";
		this.#drag.dragStartDelay = { touch: 500, mouse: 0 };
		effect( ()=>{ this.#drag.disabled = !this.field(); } );//no drag where the table has no view, or the view no such column
	}
	//Material's sort header says aria-sort on its host, the span, where it is no attribute of the role:  the cell says it, as
	//MatSort has it - the arrow's state, whatever the view's sort
	ngAfterViewInit(){
		if( this.columns.takesFocus(this.name) )
			afterNextRender( ()=>this.menuButton()?.nativeElement.focus(), {injector: this.#injector} );
		const label = this.label()?.nativeElement;
		if( !label )
			return;
		const take = ()=>{
			const sort = label.getAttribute( "aria-sort" );
			if( sort==null )
				return;
			this.#sorted.set( sort );
			label.removeAttribute( "aria-sort" );
		};
		this.#scrubber = new MutationObserver( take );
		this.#scrubber.observe( label, {attributes: true, attributeFilter: ["aria-sort"]} );
		take();
	}
	ngOnDestroy(){ this.#scrubber?.disconnect(); }
	#scrubber:MutationObserver|undefined;
	#sorted = signal<string>( "none" );
	get name():string{ return this.#column.name; }
	sortable = input<boolean>( true );
	label = viewChild( 'label', {read: ElementRef<HTMLElement>} );//its element:  the reference alone is the MatSortHeader
	menuButton = viewChild( 'menuButton', {read: ElementRef<HTMLButtonElement>} );
	field = computed<ViewField|undefined>( ()=>this.columns.field(this.name) );
	title = computed<string>( ()=>this.field()?.displayName ?? this.name );
	place = computed<number>( ()=>this.columns.place(this.name) );
	count = computed<number>( ()=>this.columns.shown().length );
	ariaSort = computed<string|null>( ()=>this.sortable() ? this.#sorted() : null );
	//kept from the cell:  a press would start its drag
	stop( event:Event ){ event.stopPropagation(); }
	moveBy( by:number ){ this.columns.move( this.name, this.place()+by, true ); }
	hide(){ this.columns.hide( this.name, true ); }

	//The width now:  as drawn, or - nothing laid out - as the view has it, else unknown, and the floor to work from
	#drawn():number|undefined{
		const flex = this.columns.flex( this.name );
		return this.#el.nativeElement.getBoundingClientRect().width || ( flex ? new Flex( flex ).px : undefined );
	}
	#width():number{ return this.#drawn() ?? View.minWidth; }
	static readonly step = 32;
	wider(){ this.#by( ViewColumnHeader.step ); }
	narrower(){ this.#by( -ViewColumnHeader.step ); }
	#by( step:number ){
		const from = this.#width();
		this.columns.width( this.name, from+step, from );
	}
	get atFloor():boolean{ return ( this.#drawn() ?? Infinity )<=View.minWidth; }

	//The handle's drag:  the pointer is captured, so the moves are the handle's wherever the pointer goes.  The width is drawn as
	//the pointer goes, and goes to the view's draft once, where it is let go.
	#resize:{x:number, from:number, width:number}|undefined;
	onResizeStart( event:PointerEvent ){
		if( event.button )
			return;
		event.preventDefault();
		event.stopPropagation();
		( <HTMLElement>event.target ).setPointerCapture?.( event.pointerId );
		const from = this.#width();
		this.#resize = { x: event.clientX, from, width: from };
	}
	onResizeMove( event:PointerEvent ){
		if( !this.#resize )
			return;
		this.#resize.width = this.#resize.from+event.clientX-this.#resize.x;
		this.columns.preview( this.name, View.widthOf(this.#resize.width, this.#resize.from) );
	}
	onResizeEnd(){
		const resize = this.#resize;
		this.#endResize();
		if( resize )
			this.columns.width( this.name, resize.width, resize.from );
	}
	//the browser took the pointer:  the width as it was
	onResizeCancel(){ this.#endResize(); }
	onResizeReset(){
		this.#endResize();
		this.columns.restoreWidth( this.name );
	}
	#endResize(){
		this.#resize = undefined;
		this.columns.preview( this.name );
	}
}