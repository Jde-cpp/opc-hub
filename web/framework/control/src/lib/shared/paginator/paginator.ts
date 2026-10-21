import {Component, input, model, signal, effect, output, computed } from '@angular/core';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';

export class PageEvent{
	constructor( paginator:Paginator ){
		this.startIndex = paginator.startIndex();
		this.pageSize = paginator.pageSize();
	}
	startIndex:number;
	pageSize:number=50;
}

@Component({
	selector: 'paginator',
	templateUrl: './paginator.html',
	styleUrls: ['./paginator.scss'],
	imports:[MatIconModule, MatButtonModule, MatInputModule]
})
export class Paginator{
	constructor(){
		effect( ()=>{
			this.previousPageIndex.set( this.pageIndex() );
		} );
	}
	onFirstPage(){
		this.startIndex.set( 0 );
		this.onPageEvent.emit( new PageEvent(this) );
	}
	onPreviousPage(){
		this.startIndex.update( value => Math.max(value-this.pageSize(), 0) );
		this.onPageEvent.emit( new PageEvent(this) );
	}
	onPrevItem(){ this.startIndex.update( value => Math.max( value-1, 0 ) ); this.onPageEvent.emit(new PageEvent(this)); }
	onNextItem(){ this.startIndex.update( value => value+1 ); this.onPageEvent.emit(new PageEvent(this)); }
	onNextPage(){ this.startIndex.update( value => value+this.pageSize() ); this.onPageEvent.emit(new PageEvent(this)); }
	onLastPage(){
		if( !this.length() )
			return;
		//ceil-1 rather than length-length%pageSize, which lands on length itself when the last page is exactly full
		this.startIndex.set( (Math.ceil(this.length()!/this.pageSize())-1)*this.pageSize() );
		this.onPageEvent.emit( new PageEvent(this) );
	}
	firstItemShowing = computed( () => this.startIndex()==0 );
	isLastPage = computed( () => this.length() && this.startIndex()+this.pageSize()>=this.length()! );
	onChangePageSize(value:number){
		this.pageSize.set( value );
		this.onPageEvent.emit( new PageEvent(this) );
	}
	hidePageLength = input<boolean>( true );
	length = input<number>();
	get lengthDescription(){ return this.length() ? ` of ${this.length()}` : ''; }
	startIndex = model<number>(0);
	pageSize = model.required<number>();
	pageIndex = model<number>(0);
	previousPageIndex = signal<number>(0);
	showFirstLastButtons = input<boolean>( true );
	onPageEvent = output<PageEvent>();
	startNumber = computed( () => {
		return this.startIndex()+1;
	});
	endIndex = computed( () => {
		let endIndex = this.startIndex()+this.pageSize()-1;
		return this.length() ? Math.min( endIndex, this.length()!-1 ) : endIndex;
	});
	endNumber = computed( () => {
		return this.endIndex()+1;
	});
}