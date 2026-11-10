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
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { SnackbarService } from '../snackbar/snackbar-service';

//A ViewSet's face, the same over every list (views spec *The view bar*):  the tabs row - a tab per view, the view menu on the
//active one's chevron, `+ New view` after them, the tabs dragged into order - over the list toolbar, whose zones are the host's
//own controls left (`[left]`) and, right, Refresh, the view editor's button, the host's (`[right]`) and the row count, with the
//reload bar under it.  `+ New view` and the rename box stand outside the tablist, which may hold tabs alone.  The toolbar changes
//the set itself and says when a write failed;  the host hears `viewChange` when the view on screen is another, and re-reads unless
//its rows are a computed of it or the view shows the rows it did (View.alike).  A view with a draft shows it, a dot after its name,
//and Save and Discard stand in the centre zone, where the filter box is to be, and in the menu.  A pick list has no tabs row:  its
//menu opens from a button showing the view's name, `Views` first, and nothing that makes, moves or removes a view.
@Component({
	selector: 'view-toolbar',
	templateUrl: './view-toolbar.html',
	styleUrl: './view-toolbar.scss',
	imports: [CdkDrag, CdkDropList, MatButton, MatIcon, MatIconButton, MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger, MatProgressBar, MatTabLink, MatTabNav, MatToolbar, MatTooltip, NgTemplateOutlet]
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

	//The view being renamed, by its tab's key:  the set may republish its views meanwhile.  The box lies over the tab, where it was.
	renaming = signal<number|undefined>( undefined );
	renamed = computed<View|undefined>( ()=>{
		const key = this.renaming();
		return key===undefined ? undefined : this.views().views().find( v=>this.#tabKey(v)==key );
	} );
	renameAt = signal<{left:number, width:number}>( {left: 0, width: 0} );
	renameInput = viewChild<ElementRef<HTMLInputElement>>( 'renameInput' );
	tabsRow = viewChild<ElementRef<HTMLElement>>( 'tabsRow' );
	links = viewChildren( MatTabLink );

	label( view:View ):string{ return view.name ?? "Default"; }
	//the menu's sizes, and the view's own where it is none of them, as the logs' 21
	pageSizes = computed<number[]>( ()=>{
		const limit = this.views().current().limit;
		return ViewToolbar.pageSizes.includes( limit ) || !limit ? ViewToolbar.pageSizes : [...ViewToolbar.pageSizes, limit].sort( (a,b)=>a-b );
	} );
	static readonly pageSizes = [10, 25, 50, 100, 250, 500];
	//a change to the draft, as any other
	pageSize( size:number ){
		if( size==this.views().current().limit )
			return;
		this.views().edit( view=>{ view.limit = size; } );
		this.viewChange.emit();
	}
	save(){
		const view = this.views().current();
		this.#change( (set, shown)=>set.save(view, shown), "Could not save view." );
	}
	discard(){
		if( this.views().discard(this.views().current()) )
			this.viewChange.emit();
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
		this.renaming.set( this.#tabKey(this.views().current()) );
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
	//what the tabs row tracks a tab by:  its view's number, which its draft shares
	#tabKey( view:View ):number{ return view.number; }
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