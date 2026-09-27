import { ChangeDetectorRef, Component, ElementRef, OnDestroy, OnInit, QueryList, Signal, ViewChild, ViewChildren, WritableSignal, input, signal, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import {FormControl, FormsModule, ReactiveFormsModule} from '@angular/forms';
import { MatButtonModule } from "@angular/material/button";
import {type MatDatepickerInputEvent, MatDatepickerModule} from '@angular/material/datepicker';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTable, MatTableModule } from '@angular/material/table';
import { Days, Filter, Operator, View, FieldFilter, ViewField } from '../../../../../model/ql/view';
import { StringUtils } from '../../../../../utils/string-utils';
import {MatAutocompleteModule, type MatAutocompleteSelectedEvent} from '@angular/material/autocomplete';
import {MatCheckboxModule} from '@angular/material/checkbox';
import { MatChipsModule, type MatChipInputEvent } from "@angular/material/chips";
import {MatFormFieldModule} from '@angular/material/form-field';
import { MatInput } from "@angular/material/input";
import { verify } from '../../../../../utils/utils';
import { Field } from '../../../../../model/ql/schema/field';
import { TableSchema } from '../../../../../model/ql/schema/table-schema';
import { BehaviorSubject, from, map, Observable, startWith, Subject } from 'rxjs';
import { COMMA, ENTER } from '@angular/cdk/keycodes';

type ColumnFilter = {field:Field, filter: Filter, displayName:string};
@Component( {
		selector: 'ql-list-settings-filter',
		styleUrls: ['ql-list-settings-filter.scss'],
		templateUrl: './ql-list-settings-filter.html',
		host: {class:'main-content.mat-drawer-container.my-content'},
			imports: [CommonModule, FormsModule, MatAutocompleteModule, MatButtonModule, MatCheckboxModule, MatChipsModule, MatDatepickerModule, MatFormFieldModule, MatIconModule, MatSelectModule, MatTableModule, MatInputModule, ReactiveFormsModule],
})
export class QLListSettingsFilter implements OnInit{
	private cdr:ChangeDetectorRef = inject( ChangeDetectorRef );

	ngOnInit(){
		for( let fieldFilter of this.view().fieldFilters ){
			this.dataSource.push( { field: fieldFilter.field, filter: View.copyFilter(fieldFilter.filter), displayName: this.columns()[fieldFilter.field.name] } );//edit a copy: every handler below mutates col.filter in place, and holding the live view's Filter meant those edits survived Cancel
			this.addSignals( fieldFilter.field, fieldFilter.filter.operator, fieldFilter.filter.value );
		}
		this.dataSource.push( {field: undefined as any, filter: {operator: Operator.None, value: []}, displayName: ""} );//placeholder add-row: the template renders the column-select only when field is falsy (new Field({}) also threw in Field's ctor)
	}

	onOperatorChange( op: Operator, col: ColumnFilter ){
		this.operatorSignals.get(col.field.name)!.set(op);
		col.filter.operator = op;
	}
	filter( col:ColumnFilter ):string[]{
		return col.filter.value as string[];
	}
	onRemove(col:ColumnFilter, item: string){
		//let arg = this.args.get(col.field.name);
		col.filter.value.splice( col.filter.value.indexOf(item), 1 );
		this.autoCompleteSubjects.get(col.field.name)!.next(this.colSuggestions(col, ""));
	}
	onAddValue( value:Filter['value'][number], col: ColumnFilter ){
		col.filter.value.push( value );
		let index = 0;
		for( let input of this.autoCompleteInputs.keys() ){
			if( input == col.field.name )
				break;
			++index;
		}
		let fc = this.autoCompleteInputs.get(col.field.name)!;
		fc.reset();
		this.inputElements.toArray()[index].nativeElement.value = "";
	}
	onAddFilter( columnName: string ){
		verify( !this.dataSource.find( c=>c.field?.name == columnName )?.field );
		let field = this.schema().fields.find(f=>f.name == columnName)!;
		const index = this.dataSource.length-1;
		this.dataSource.push( this.dataSource[index] );
		let operator = field.isDateTime ? Operator.Greater : Operator.In;
		this.dataSource[index] = { field: field, filter: {operator: operator, value: []}, displayName: this.columns()[field.name] };
		this.addSignals( field, operator );
		this.table.renderRows();
	}
	addSignals( field: Field, operator: Operator, value:Filter['value'] = [] ){
		this.operatorSignals.set( field.name, signal(operator) );
		if( field.isNullable )//from the saved value:  a reopened filter showed both boxes clear, so ticking one added a second marker (reviews/m3-closing.md #35)
			this.nullSignals.set( field.name, signal(value.includes("<not null>") ? NullCriteria.NonNull : value.includes("<null>") ? NullCriteria.Null : NullCriteria.None) );
	}
	//A DateTime filter is one of none, null, not null or a date - never several.  They shared one array, so a date and a
	//marker could both end up in it and the query sent `{gt:"<null>"}` (reviews/m3-closing.md #35).
	onChangeDate( event:MatDatepickerInputEvent<Date>, col: ColumnFilter ){
		if( !event.value )
			col.filter.value = [];
		else
			col.filter.value = [col.filter.operator==Operator.Greater ? new Days(event.value) : event.value];
		this.nullSignals.get( col.field.name )?.set( NullCriteria.None );
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

	autoCompleteValues(col:ColumnFilter): Observable<string[]>{
		return this.autoCompleteSubjects.get(col.field.name)!.asObservable();
	}

	input(col: ColumnFilter): FormControl{
		let input = this.autoCompleteInputs.get(col.field.name);
		if( input )
			return input;
		input = new FormControl();
		this.autoCompleteInputs.set( col.field.name, input );
		const subject = new BehaviorSubject<string[]>([]);
		this.autoCompleteSubjects.set( col.field.name, subject );
		input.valueChanges.pipe(
			map( value=>{
				return this.colSuggestions(col, value);
			})
		).subscribe( suggestions=>{
			subject.next(suggestions);
		});
		subject.next( this.colSuggestions(col, "") );
		return input;
	}
	cellClick( row:ColumnFilter ){
		this.selection.set( row );
	}
	isSelected( row:ColumnFilter ){
		return this.selection() === row;
	}
	operatorSignal(col:ColumnFilter):WritableSignal<Operator>{
		return this.operatorSignals.get(col.field.name)!;
	}
	inputType( field:Field ):string{
		return field.isNumber ?  "number" : "text";
	}
	onDelete(col:ColumnFilter){
		this.autoCompleteInputs.delete(col.field.name);
		this.operatorSignals.delete(col.field.name);
		if( col.field.isNullable )
			this.nullSignals.delete(col.field.name);
		this.dataSource.splice( this.dataSource.indexOf(col), 1 );
		this.table.renderRows();
	}

	nullSignal(colName: string):WritableSignal<NullCriteria>{
		return this.nullSignals.get(colName)!;
	}
	onNullToggle( add:boolean, col: ColumnFilter ){
		col.filter.value = add ? ["<null>"] : [];
		this.nullSignals.get( col.field.name )!.set( add ? NullCriteria.Null : NullCriteria.None );
	}
	onNonNullToggle( add:boolean, col: ColumnFilter ){
		col.filter.value = add ? ["<not null>"] : [];
		this.nullSignals.get( col.field.name )!.set( add ? NullCriteria.NonNull : NullCriteria.None );
	}
	//A live-toggle column (the Resources page's Enforced) is a yes/no:  its null is the switch ON, so "null" read backwards under
	//that header, and a date or an operator means nothing there.
	liveToggle( col:ColumnFilter ):boolean{ return !!this.view().fields.find( f=>f.name==col.field.name )?.liveToggle; }
	nullLabel( col:ColumnFilter ):string{ return this.liveToggle(col) ? col.displayName : "null"; }
	nonNullLabel( col:ColumnFilter ):string{ return this.liveToggle(col) ? `Not ${col.displayName.toLowerCase()}` : "not null"; }
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
	autoCompleteSubjects = new Map<string, BehaviorSubject<string[]>>();
	operatorSignals = new Map<string, WritableSignal<Operator>>();
	nullSignals = new Map<string, WritableSignal<NullCriteria>>();

	autoCompleteInputs = new Map<string, FormControl>();
  readonly separatorKeysCodes: number[] = [ENTER, COMMA];
	@ViewChildren('dynamicInput') inputElements!: QueryList<ElementRef>;

  MyOperator = Operator;
	NullCriteria = NullCriteria;
	operatorList = Object.values(Operator);
  @ViewChild('table', {static: true}) table!: MatTable<ViewField>;
}
enum NullCriteria{
	None,
	Null,
	NonNull
}