import { Component, computed, inject, input, output } from '@angular/core';
import { MatButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger } from '@angular/material/menu';
import { ScreenSize } from 'jde-spa';
import { orderSlices, SliceCount, sliceKey, SliceValue } from '../../model/ql/slice';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';

type SliceRow = { key:string, value:SliceValue, label:string, count:number|undefined, held:boolean };

//A view's slice (views spec *Slice by*), at the left of a list's table inside `.jde-sliced` (_general.scss):  the slice column's
//name, which switches it, *All* with the list's count, then a row per value with its count.  A click narrows the list to the
//value and a second returns to *All*.  The value is the browser's (ViewSet.setSlice) and the column a change to the view's draft;
//the host hears `viewChange` for either, as it does from the toolbar, and gives the counts:  a GraphQL list's from its `groupBy`
//query, undefined until it is answered, and a list that holds its rows its own count of them.  Nothing where the view is not
//sliced.  On a small screen the panel is a `Slice` button over the table, whose menu is the same rows, and on a phone it is
//absent, the list narrowed as it was left.
@Component({
	selector: 'slice-panel',
	templateUrl: './slice-panel.html',
	styleUrl: './slice-panel.scss',
	imports: [MatButton, MatIcon, MatMenu, MatMenuContent, MatMenuItem, MatMenuTrigger],
	host: { '[class.folded]': 'folded()', '[style.display]': 'field() ? null : "none"' }
})
export class SlicePanel{
	#screen = inject( ScreenSize );
	views = input.required<ViewSet<View>>();
	columns = input<Record<string,string>>( {} );//the host's, by name with what the header reads
	counts = input<readonly SliceCount[]|undefined>();
	failed = input<boolean>( false );//the counts could not be read
	viewChange = output<void>();
	folded = this.#screen.isSmall;
	field = computed<{name:string, label:string}|undefined>( ()=>{
		const view = this.views().current(), name = view?.sliceBy;
		return name && this.views().slices && !this.#screen.isExtraSmall() ? {name, label: view.columnLabel( name, this.columns() )} : undefined;
	} );
	choices = computed<{name:string, label:string}[]>( ()=>this.views().current().sliceChoices( this.columns() ) );
	held = computed<SliceValue|undefined>( ()=>this.views().slice() );
	//the values in the panel's order (orderSlices), the one held among them whatever the counts say
	rows = computed<SliceRow[]>( ()=>{
		const counts = this.counts(), held = this.held(), label = this.field()?.label ?? "";
		return orderSlices( counts ?? [], this.views().current().sliceOrder(), held ).map( c=>({
			key: sliceKey( c.value ), value: c.value, label: c.value===null ? `No ${label}` : `${c.value}`, count: counts ? c.count : undefined, held: held!==undefined && sliceKey( held )==sliceKey( c.value )
		}) );
	} );
	total = computed<number|undefined>( ()=>this.counts()?.reduce( (sum, c)=>sum+c.count, 0 ) );
	heldLabel = computed<string|undefined>( ()=>this.rows().find( r=>r.held )?.label );

	all(){ this.#set( undefined ); }
	pick( row:SliceRow ){ this.#set( row.held ? undefined : row.value ); }
	#set( value:SliceValue|undefined ){
		if( value===this.held() )
			return;
		this.views().setSlice( value );
		this.viewChange.emit();
	}
	sliceBy( name:string|undefined ){
		if( name==this.views().current().sliceBy )
			return;
		this.views().edit( view=>{ view.sliceBy = name; } );
		this.viewChange.emit();
	}
}