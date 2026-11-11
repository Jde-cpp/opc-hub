import { Component, ElementRef, computed, effect, inject, input, output, signal, viewChild, viewChildren } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import { CdkDrag, CdkDragDrop, CdkDropList } from '@angular/cdk/drag-drop';
import { MatButton, MatIconButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger } from '@angular/material/menu';
import { MatProgressBar } from '@angular/material/progress-bar';
import { MatTabLink, MatTabNav, MatTabNavPanel } from '@angular/material/tabs';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { ScreenSize } from 'jde-spa';
import { FilterHost } from '../../model/ql/filter-query';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { FilterBox } from '../filter-box/filter-box';
import { SnackbarService } from '../snackbar/snackbar-service';

//A ViewSet's face, the same over every list (views spec *The view bar*):  the tabs row - a tab per view, the view menu on the
//active one's chevron, `+ New view` after them, the tabs dragged into order - over the list toolbar, whose zones are the host's
//own controls left (`[left]`) and, right, Refresh, the view editor's button, the host's (`[right]`) and the row count, with the
//reload bar under it.  `+ New view` and the rename box stand outside the tablist, which may hold tabs alone.  The toolbar changes
//the set itself and says when a write failed;  the host hears `viewChange` when the view on screen is another, and re-reads unless
//its rows are a computed of it or the rows it read hold what the view asks (View.answers).  The centre zone is the filter box, where the view
//takes a filter (View.filterHost):  what is typed is a change to the draft.  A view with a draft shows it, a dot after its name,
//and Save and Discard stand at the end of the centre zone, and in the menu.  On a phone the centre zone is a row of its own, and is
//rendered after the right zone, so the tab order is the rows'.  A pick list has no tabs row:  its menu opens from a button showing
//the view's name, `Views` first, and nothing that makes, moves or removes a view.
@Component({
	selector: 'view-toolbar',
	templateUrl: './view-toolbar.html',
	styleUrl: './view-toolbar.scss',
	imports: [CdkDrag, CdkDropList, FilterBox, MatButton, MatIcon, MatIconButton, MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger, MatProgressBar, MatTabLink, MatTabNav, MatToolbar, MatTooltip, NgTemplateOutlet]
})
export class ViewToolbar{
	#snackbar = inject( SnackbarService );
	constructor(){
		effect( ()=>{//the rename box, as it appears
			const input = this.renameInput()?.nativeElement;
			if( input ){
				input.focus();
				input.select();
			}
		} );
	}
	views = input.required<ViewSet<View>>();
	tabPanel = input<MatTabNavPanel>();//the host's, around its table:  Material requires one of a tabs row
	tabs = input<boolean>( true );//false for a pick list
	paged = input<boolean>( false );//the host reads its rows a page at a time:  the menu has `Page size`
	refreshing = input<boolean>( false );
	count = input<number>();//the rows shown, where that is the whole count - none while a pager carries the range
	viewChange = output<void>();//the view on screen is another.  Not `change`:  a native change event bubbling out of a projected input would reach the same binding
	edit = output<void>();
	refresh = output<void>();
	filterValues = input<Record<string,readonly unknown[]>>( {} );//the rows' values by column, which the filter box suggests
	filterHost = computed<FilterHost|undefined>( ()=>this.views().current()?.filterHost() );//none:  no box - the logs
	box = viewChild( FilterBox );//Save takes what is typed in it, and Discard drops it
	isExtraSmall = inject( ScreenSize ).isExtraSmall;
	//the box's text:  a change to the draft, as any other
	filter( text:string ){
		if( text==this.views().current().filter )
			return;
		this.views().edit( view=>{ view.filter = text; } );
		this.viewChange.emit();
	}

	//The view being renamed, by its number, which its draft shares:  the set may republish its views meanwhile.  The box lies over
	//the tab, where it was.
	renaming = signal<number|undefined>( undefined );
	renamed = computed<View|undefined>( ()=>{
		const number = this.renaming();
		return number===undefined ? undefined : this.views().views().find( v=>v.number==number );
	} );
	renameAt = signal<{left:number, width:number}>( {left: 0, width: 0} );
	renameInput = viewChild<ElementRef<HTMLInputElement>>( 'renameInput' );
	tabsRow = viewChild<ElementRef<HTMLElement>>( 'tabsRow' );
	links = viewChildren( MatTabLink );
	viewButton = viewChild( 'viewButton', {read: ElementRef<HTMLButtonElement>} );//its element:  the reference alone is the MatButton

	label( view:View ):string{ return view.name ?? "Default"; }
	//The `Fields` checklist:  the host's columns, by name with what the header reads - those the view shows first, in its order -
	//and, where the host lets its rows be ticked, *Select* ahead of them.  A tick is a change to the draft, which leaves the list
	//in the order it opened in:  an item moved under the focus or the pointer would lose the one and mislead the other.  The last
	//column shown stays ticked.
	columns = input<Record<string,string>>( {} );
	selectable = input<boolean>( false );
	#listed = computed<{name:string, label:string, shown:boolean}[]>( ()=>{
		const offered = this.columns(), view = this.views().current();
		const shown = view.fields.filter( f=>f.displayed && f.name in offered );
		const rest = Object.keys( offered ).filter( name=>!shown.some( f=>f.name==name ) );
		return [...shown.map( f=>({name: f.name, label: f.displayName ?? offered[f.name], shown: true}) ), ...rest.map( name=>({name, label: offered[name], shown: false}) )];
	} );
	#opened = signal<string[]|undefined>( undefined );//the order while the checklist is open
	fields = computed<{name:string, label:string, shown:boolean}[]>( ()=>{
		const listed = this.#listed(), order = this.#opened();
		const at = ( name:string )=>{ const i = order!.indexOf( name ); return i<0 ? order!.length : i; };
		return order ? [...listed].sort( (a,b)=>at(a.name)-at(b.name) ) : listed;
	} );
	fieldsOpened(){ this.#opened.set( this.#listed().map( f=>f.name ) ); }
	fieldsClosed(){ this.#opened.set( undefined ); }
	shownCount = computed<number>( ()=>this.views().current().fields.filter( f=>f.displayed ).length );
	//one the view lacks is the route's field for it, its width and chip with it
	toggleField( field:{name:string, label:string, shown:boolean}, event:Event ){
		event.stopPropagation();//the menu stays open:  a checklist takes several ticks
		const route = this.views().routeDefault?.fields.find( f=>f.name==field.name );
		this.views().edit( view=>view.setDisplayed(field.name, !field.shown, field.label, route) );
		this.viewChange.emit();
	}
	toggleSelector( event:Event ){
		event.stopPropagation();
		this.views().edit( view=>{ view.showSelector = !view.showSelector; } );
		this.viewChange.emit();
	}
	//the menu's sizes, and the view's own where it is none of them, as the logs' 21
	pageSizes = computed<number[]>( ()=>{
		const limit = this.views().current().limit;
		return ViewToolbar.sizes.includes( limit ) || !limit ? ViewToolbar.sizes : [...ViewToolbar.sizes, limit].sort( (a,b)=>a-b );
	} );
	static readonly sizes = [10, 25, 50, 100, 250, 500];
	//a change to the draft, as any other
	pageSize( size:number ){
		if( size==this.views().current().limit )
			return;
		this.views().edit( view=>{ view.limit = size; } );
		this.viewChange.emit();
	}
	//The draft over its view, as it stands once the set makes the change.  The bar's Discard and Save go with the draft, so they hand
	//the focus to the view's tab, or a pick list's button, rather than drop it to the page;  the menu's hand it back to the chevron.
	save( fromBar=false ){
		this.box()?.flush();
		const view = this.views().current();
		this.#change( (set, shown)=>set.saveDraft(view, shown), "Could not save view." );
		if( fromBar )
			this.#focusView();
	}
	discard( fromBar=false ){
		this.box()?.revert();
		if( this.views().discard(this.views().current()) )
			this.viewChange.emit();
		if( fromBar )
			this.#focusView();
	}
	#focusView(){
		if( this.tabs() )
			this.links()[this.views().index()]?.focus();
		else
			this.viewButton()?.nativeElement.focus();
	}
	//the tab clicked, the active one again being nothing:  the host re-reads on a change
	onTab( index:number ){
		if( index!=this.views().index() && this.views().select(index) )
			this.viewChange.emit();
	}
	onDrop( event:CdkDragDrop<unknown> ){
		const view = this.views().views()[event.previousIndex];
		if( view && event.previousIndex!=event.currentIndex )
			this.#change( (set, shown)=>set.move(view, event.currentIndex, shown), "Could not move view." );
	}
	//the chevron's events, kept from the tab:  its click would select, Space and Enter on it click, the arrows move between tabs,
	//and a press on it, by mouse or by touch, would start a drag
	stop( event:Event ){ event.stopPropagation(); }

	add(){ this.#change( (set, shown)=>set.add(shown), "Could not add view." ); }
	duplicate(){
		const view = this.views().current();
		this.#change( (set, shown)=>set.duplicate(view, shown), "Could not duplicate view." );
	}
	canMove( by:number ):boolean{
		const at = this.#place( this.views().current() ), to = at+by;
		return at>=0 && to>=0 && to<this.views().views().length;
	}
	move( by:number ){
		const view = this.views().current();
		this.#change( (set, shown)=>set.move(view, this.#place(view)+by, shown), "Could not move view." );
	}
	remove(){
		const view = this.views().current();
		this.#change( (set, shown)=>set.remove(view, shown), "Could not delete view." );
	}
	rename(){
		const tab = this.links()[this.views().index()]?.elementRef.nativeElement as HTMLElement|undefined, row = this.tabsRow()?.nativeElement;
		if( tab && row ){
			const at = tab.getBoundingClientRect();
			this.renameAt.set( {left: at.left-row.getBoundingClientRect().left, width: at.width} );
		}
		this.renaming.set( this.views().current().number );
	}
	onRenameKey( event:KeyboardEvent ){
		if( event.key=="Enter" )
			this.commitRename( (<HTMLInputElement>event.target).value, true );
		else if( event.key=="Escape" )
			this.cancelRename();
	}
	//Enter, or leaving the box, the focus back on the tab only for Enter:  a blur has sent it somewhere.  A blank name is no name,
	//and the name the box opened on no change, the unnamed view's *Default* among them.
	commitRename( value:string, refocus:boolean ){
		const view = this.renamed();
		this.#endRename( refocus );
		const name = value.trim();
		if( view && name && name!=this.label(view) )
			this.#change( (set, shown)=>set.rename(view, name, shown), "Could not rename view." );
	}
	cancelRename(){
		if( this.renaming()!==undefined )
			this.#endRename( true );
	}
	#endRename( refocus:boolean ){
		this.renaming.set( undefined );
		if( refocus )
			this.links()[this.views().index()]?.focus();
	}
	//the tab of the view:  the hosts with a tabs row offer every view, so it is the view's place in the set
	#place( view:View ):number{ return this.views().views().findIndex( v=>v.number==view.number ); }
	//A change through the set, which says when it puts another view on screen (ViewSet), its writes awaited and a failure said
	async #change( change:( set:ViewSet<View>, shown:()=>void )=>Promise<void>, failed:string ){
		try{
			await change( this.views(), ()=>this.viewChange.emit() );
		}
		catch( e ){
			this.#snackbar.exception( failed, e );
		}
	}
}