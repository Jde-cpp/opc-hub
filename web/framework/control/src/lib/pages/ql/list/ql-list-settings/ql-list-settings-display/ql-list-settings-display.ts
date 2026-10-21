import {CdkDragDrop, CdkDropList, CdkDrag, moveItemInArray} from '@angular/cdk/drag-drop';
import { Component, OnInit, input, signal, viewChild } from '@angular/core';
import {TableSchema}  from '../../../../../model/ql/schema/table-schema';

import { MatIcon } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatCheckbox } from '@angular/material/checkbox';
import { MatSelectModule } from '@angular/material/select';
import { MatTable, MatTableModule } from '@angular/material/table';
import { View, ViewField } from '../../../../../model/ql/view';
import { StringUtils } from '../../../../../utils/string-utils';


@Component( {
		selector: 'ql-list-settings-display',
		styleUrls: ['ql-list-settings-display.scss'],
		templateUrl: './ql-list-settings-display.html',
		imports: [CdkDrag, CdkDropList, MatCheckbox, MatIcon, MatSelectModule, MatTableModule, MatInputModule]
})
export class QLListSettingsDisplay implements OnInit{
	ngOnInit(){
		let columns:(SelectorField|ViewField)[] = [{name:"Selector", displayed:this.view().showSelector}];
		const available = this.columns();
		//the table renders view.fields in order, so list them in that order too; schema-order leftovers (not in the view) follow
		const viewNames = this.view().fields.map( f=>f.name ).filter( n=>n in available );
		const names = [...viewNames, ...Object.keys(available).filter( n=>!viewNames.includes(n) )];
		for( let name of names ){
			let field = this.view().fields.find( f=>f.name==name );
			if( field )
				columns.push( new ViewField(field) );//edit a copy: the template toggles `col.displayed` in place, so holding the live view's ViewField meant show/hide edits survived Cancel (same defect as the filter tab — see View.copyFilter)
			else
				columns.push( new ViewField({field:{name: name, hidden:true, displayName: available[name]}, schema:this.schema()}) );
		}
		this.dataSource = columns;
		this.pageSize.set( this.view().limit );
	}
	updatePageSize( value:number ){
		this.pageSize.set( value>0 ? Math.floor(value) : 1 );//the box is free text: a cleared or negative one would save as `limit:0`, which query() reads as "no limit"
	}
	cellClick( row:SelectorField|ViewField ){
		this.selection.set( row );
	}
	isSelected( row:SelectorField|ViewField ){
		return this.selection() === row;
	}
	drop( event: CdkDragDrop<string> ){
		moveItemInArray( this.dataSource, event.previousIndex, event.currentIndex || 1 );
    this.table().renderRows();
	}
	columnName( col:SelectorField|ViewField|undefined ): string{
		return col ? StringUtils.idToDisplay(col.name) : "Selector";
	}

	get columnNames(){ return ["position", "select", "name"] };
	dataSource:(SelectorField|ViewField)[] = [];
	selection = signal<SelectorField|ViewField|null>( null );//only ever compared by identity in isSelected
	pageSize = signal<number>( 25 );//ngOnInit seeds it from view().limit; ql-list-settings.getView reads it back

	view = input.required<View>();
	paged = input<boolean>( true );//the host reads its rows a page at a time, so Page Size means something
	columns = input.required<Record<string,string>>();
	schema = input.required<TableSchema>();

	table = viewChild.required<MatTable<SelectorField|ViewField>>( 'table' );
}
type SelectorField = {name:"Selector", displayed:boolean, sort?:number};