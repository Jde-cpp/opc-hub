import { Component, ElementRef, effect, inject, input, output, signal, viewChild, viewChildren } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import { CdkDrag, CdkDragDrop, CdkDropList } from '@angular/cdk/drag-drop';
import { MatButton, MatIconButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatMenu, MatMenuItem, MatMenuTrigger } from '@angular/material/menu';
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
//reload bar under it.  The toolbar changes the set itself and says when a write failed;  the host hears `viewChange` when the view
//on screen is another and re-reads, unless its rows are a computed of it.  A pick list has no tabs row:  its menu opens from a
//button showing the view's name, `Views` first, and nothing that makes, moves or removes a view.
@Component({
	selector: 'view-toolbar',
	templateUrl: './view-toolbar.html',
	styleUrl: './view-toolbar.scss',
	imports: [CdkDrag, CdkDropList, MatButton, MatIcon, MatIconButton, MatMenu, MatMenuItem, MatMenuTrigger, MatProgressBar, MatTabLink, MatTabNav, MatToolbar, MatTooltip, NgTemplateOutlet]
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
	refreshing = input<boolean>( false );
	count = input<number>();//the rows shown, where that is the whole count - none while a pager carries the range
	viewChange = output<void>();//the view on screen is another.  Not `change`:  a native change event bubbling out of a projected input would reach the same binding
	edit = output<void>();
	refresh = output<void>();

	renaming = signal<View|undefined>( undefined );//the view whose tab is a name box
	renameInput = viewChild<ElementRef<HTMLInputElement>>( 'renameInput' );
	links = viewChildren( MatTabLink );

	label( view:View ):string{ return view.name ?? "Default"; }
	//the tab clicked, the active one again being nothing:  the host re-reads on a change
	onTab( index:number ){
		if( index!=this.views().index() && this.views().select(index) )
			this.viewChange.emit();
	}
	onDrop( event:CdkDragDrop<unknown> ){
		const view = this.views().views()[event.previousIndex];
		if( view && event.previousIndex!=event.currentIndex )
			this.#change( set=>set.move(view, event.currentIndex), "Could not move view." );
	}
	//the chevron's and the name box's events, kept from the tab:  its click would select, Space and Enter on it click, and the
	//arrows move between tabs;  a mouse pressed on either would start a drag
	stop( event:Event ){ event.stopPropagation(); }

	add(){ this.#change( set=>set.add(), "Could not add view." ); }
	duplicate(){
		const view = this.views().current();
		this.#change( set=>set.duplicate(view), "Could not duplicate view." );
	}
	canMove( by:number ):boolean{
		const at = this.#place( this.views().current() ), to = at+by;
		return at>=0 && to>=0 && to<this.views().views().filter( v=>!v.isAdhoc ).length;
	}
	move( by:number ){
		const view = this.views().current();
		this.#change( set=>set.move(view, this.#place(view)+by), "Could not move view." );
	}
	remove(){
		const view = this.views().current();
		this.#change( set=>set.remove(view), "Could not delete view." );
	}
	rename(){ this.renaming.set( this.views().current() ); }
	onRenameKey( event:KeyboardEvent ){
		this.stop( event );
		if( event.key=="Enter" )
			this.commitRename( (<HTMLInputElement>event.target).value );
		else if( event.key=="Escape" )
			this.cancelRename();
	}
	//Enter, or leaving the box;  a blank name is no name, and the view keeps its own
	commitRename( value:string ){
		const view = this.renaming();
		if( !view )
			return;
		this.#endRename();
		const name = value.trim();
		if( name && name!=view.name )
			this.#change( set=>set.rename(view, name), "Could not rename view." );
	}
	cancelRename(){
		if( this.renaming() )
			this.#endRename();
	}
	#endRename(){
		this.renaming.set( undefined );
		this.links()[this.views().index()]?.focus();
	}
	//the tab of the view, a twin's that of the view under it:  the hosts with a tabs row offer every view, so it is the view's
	//place in the set
	#place( view:View ):number{ return this.views().views().findIndex( v=>v.number==view.number ); }
	//A change made at once, its writes awaited and a failure said.  The host hears of the view on screen changing now and - where
	//the set was still to be read - once the change lands, as LogDetail re-reads its rows.
	async #change( change:( set:ViewSet<View> )=>Promise<void>, failed:string ){
		const set = this.views(), before = set.current();
		const writes = change( set );
		if( set.current()!==before )
			this.viewChange.emit();
		const shown = set.current();
		try{
			await writes;
		}
		catch( e ){
			this.#snackbar.exception( failed, e );
		}
		if( set.current()!==shown )
			this.viewChange.emit();
	}
}