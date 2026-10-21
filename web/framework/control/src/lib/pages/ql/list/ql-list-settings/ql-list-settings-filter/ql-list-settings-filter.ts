import { Component, OnInit, WritableSignal, input, signal, viewChild } from '@angular/core';
import { AsyncPipe, KeyValuePipe } from '@angular/common';
import {FormControl, FormsModule, ReactiveFormsModule} from '@angular/forms';
import { MatButtonModule } from "@angular/material/button";
import {type MatDatepickerInputEvent, MatDatepickerModule} from '@angular/material/datepicker';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTable, MatTableModule } from '@angular/material/table';
import { Days, FieldFilter, Filter, Operator, View, ViewField } from '../../../../../model/ql/view';
import {MatAutocompleteModule} from '@angular/material/autocomplete';
import {MatCheckboxModule} from '@angular/material/checkbox';
import { MatChipsModule } from "@angular/material/chips";
import {MatFormFieldModule} from '@angular/material/form-field';
import { verify } from '../../../../../utils/utils';
import { Field } from '../../../../../model/ql/schema/field';
import { TableSchema } from '../../../../../model/ql/schema/table-schema';
import { BehaviorSubject, map } from 'rxjs';
import { COMMA, ENTER } from '@angular/cdk/keycodes';

//a filter row with the state its cells keep:  the operator (the date's label reads it), the two boxes of a nullable column, and
//the chip input with what its autocomplete offers
type ColumnFilter = {field:Field, filter: Filter, displayName:string, operator:WritableSignal<Operator>, nullCriteria?:WritableSignal<NullCriteria>, input:FormControl, suggestions:BehaviorSubject<string[]>};
@Component( {
		selector: 'ql-list-settings-filter',
		styleUrls: ['ql-list-settings-filter.scss'],
		templateUrl: './ql-list-settings-filter.html',
			imports: [AsyncPipe, KeyValuePipe, FormsModule, MatAutocompleteModule, MatButtonModule, MatCheckboxModule, MatChipsModule, MatDatepickerModule, MatFormFieldModule, MatIconModule, MatSelectModule, MatTableModule, MatInputModule, ReactiveFormsModule],
})
export class QLListSettingsFilter implements OnInit{
	ngOnInit(){
		for( let fieldFilter of this.view().fieldFilters )
			this.dataSource.push( this.#row(fieldFilter.field, View.copyFilter(fieldFilter.filter)) );//edit a copy: every handler below mutates col.filter in place, and holding the live view's Filter meant those edits survived Cancel
		this.dataSource.push( <ColumnFilter>{field: undefined, filter: {operator: Operator.None, value: []}, displayName: ""} );//placeholder add-row: the template renders the column-select only when field is falsy (new Field({}) also threw in Field's ctor)
	}

	onOperatorChange( op: Operator, col: ColumnFilter ){
		col.operator.set( op );
		col.filter.operator = op;
	}
	filter( col:ColumnFilter ):string[]{
		return col.filter.value as string[];
	}
	onRemove(col:ColumnFilter, item: string){
		col.filter.value.splice( col.filter.value.indexOf(item), 1 );
		col.suggestions.next( this.colSuggestions(col, "") );
	}
	//`box` is the row's own input:  the chip grid is the form field's control, so the autocomplete's write of the reset lands on
	//the grid and leaves the typed text in the box.
	onAddValue( value:Filter['value'][number], col:ColumnFilter, box:HTMLInputElement ){
		col.filter.value.push( value );
		col.input.reset();
		box.value = "";
	}
	//the filters the rows set, for the view the dialog builds:  each a copy, or the view would share the rows' live value arrays
	fieldFilters():FieldFilter[]{ return this.dataSource.filter( c=>c.field ).map( c=>({field: c.field, filter: View.copyFilter(c.filter)}) ); }
	onAddFilter( columnName: string ){
		verify( !this.dataSource.find( c=>c.field?.name == columnName )?.field );
		let field = this.schema().fields.find(f=>f.name == columnName)!;
		const index = this.dataSource.length-1;
		this.dataSource.push( this.dataSource[index] );
		this.dataSource[index] = this.#row( field, {operator: field.isDateTime ? Operator.Greater : Operator.In, value: []} );
		this.table().renderRows();
	}
	#row( field:Field, filter:Filter ):ColumnFilter{
		const value = filter.value;
		const row:ColumnFilter = { field, filter, displayName: this.columns()[field.name], operator: signal( filter.operator ), input: new FormControl(), suggestions: new BehaviorSubject<string[]>([]) };
		if( field.isNullable )//from the saved value:  a reopened filter showed both boxes clear, so ticking one added a second marker (reviews/m3-closing.md #35)
			row.nullCriteria = signal( value.includes("<not null>") ? NullCriteria.NonNull : value.includes("<null>") ? NullCriteria.Null : NullCriteria.None );
		row.input.valueChanges.pipe( map(text=>this.colSuggestions(row, text)) ).subscribe( suggestions=>row.suggestions.next(suggestions) );
		row.suggestions.next( this.colSuggestions(row, "") );
		return row;
	}
	//A DateTime filter is one of none, null, not null or a date - never several.  They shared one array, so a date and a
	//marker could both end up in it and the query sent `{gt:"<null>"}` (reviews/m3-closing.md #35).
	onChangeDate( event:MatDatepickerInputEvent<Date>, col: ColumnFilter ){
		if( !event.value )
			col.filter.value = [];
		else
			col.filter.value = [col.filter.operator==Operator.Greater ? new Days(event.value) : event.value];
		col.nullCriteria?.set( NullCriteria.None );
	}
	dateValue( col: ColumnFilter ): Date|undefined{
		if( !col.filter.value.length )
			return undefined;
		for( let val of col.filter.value ){
			if( val instanceof Days )
				return (val as Days).fromNow();
			else if( val instanceof Date )
				return val;
		}
		return undefined;
	}
	colSuggestions(col:ColumnFilter, value:string):string[]{
		let suggestions = this.suggestions()[col.field.name] as string[];
		if( !suggestions ) //filter column not shown.
			return [];
		const text = (value ?? "").toString().trim().toLowerCase();
		let result = suggestions.filter( s=>{
			const existing = col.filter.value;
			if( existing.includes(s) )
				return false;
			if( text && !String(s).toLowerCase().includes(text) )//String(): ql-list.colSuggestions pushes raw row values, so numbers land here too
				return false;
			if( ["<null>", "<not null>"].includes(s) ){
				if( [Operator.Less, Operator.Greater].includes(col.filter.operator) )
					return false;
				else if( s=="<not null>" )
					return !existing.length;
			}
			return existing.indexOf( "<not null>" )==-1;
		});
		return result;
	}

	cellClick( row:ColumnFilter ){
		this.selection.set( row );
	}
	isSelected( row:ColumnFilter ){
		return this.selection() === row;
	}
	inputType( field:Field ):string{
		return field.isNumber ?  "number" : "text";
	}
	onDelete(col:ColumnFilter){
		this.dataSource.splice( this.dataSource.indexOf(col), 1 );
		this.table().renderRows();
	}

	onNullToggle( add:boolean, col: ColumnFilter ){
		col.filter.value = add ? ["<null>"] : [];
		col.nullCriteria!.set( add ? NullCriteria.Null : NullCriteria.None );
	}
	onNonNullToggle( add:boolean, col: ColumnFilter ){
		col.filter.value = add ? ["<not null>"] : [];
		col.nullCriteria!.set( add ? NullCriteria.NonNull : NullCriteria.None );
	}
	//A live-toggle column (the Resources page's Enforced) is a yes/no:  its null is the switch ON, so "null" read backwards under
	//that header, and a date or an operator means nothing there.
	liveToggle( col:ColumnFilter ):boolean{ return !!this.view().fields.find( f=>f.name==col.field.name )?.liveToggle; }
	nullLabel( col:ColumnFilter ):string{ return this.liveToggle(col) ? col.displayName : col.field.name=="deleted" ? "Not deleted" : "null"; }
	nonNullLabel( col:ColumnFilter ):string{ return this.liveToggle(col) ? `Not ${col.displayName.toLowerCase()}` : col.field.name=="deleted" ? "Deleted" : "not null"; }
	get columnNames(){ return ["name", "operation", "filter", "delete"] };//
	get unFilteredColumns():Record<string,string>{
		let columns:Record<string,string> = {};
		for( const [name,display] of Object.entries(this.columns()) ){
			if( !this.dataSource.some(c=>c.field?.name == name) )
				columns[name] = display;
		}
		return columns;
	}
	dataSource:ColumnFilter[] = [];
	selection = signal<ColumnFilter|null>( null );//a ColumnFilter, not a ViewField - the `any` on cellClick hid that; only ever compared by identity
	view = input.required<View>();
	columns = input.required<Record<string,string>>();
	schema = input.required<TableSchema>();
	suggestions = input.required<Record<string,any[]>>();
	excludedColumns = input.required<string[]>();
  readonly separatorKeysCodes: number[] = [ENTER, COMMA];

  MyOperator = Operator;
	NullCriteria = NullCriteria;
	table = viewChild.required<MatTable<ViewField>>( 'table' );
}
enum NullCriteria{
	None,
	Null,
	NonNull
}