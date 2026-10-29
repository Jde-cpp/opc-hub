import { Component, computed, input, model, output } from '@angular/core';
import { DatePipe } from '@angular/common';
import { SelectionModel } from '@angular/cdk/collections';
import { MatTableModule } from '@angular/material/table';
import { MatSortModule, Sort } from '@angular/material/sort';
import {FieldKind} from '../../../model/ql/schema/field';
import { MatCheckbox } from '@angular/material/checkbox';
import { MatChipsModule } from '@angular/material/chips';
import { MatIcon } from '@angular/material/icon';
import { MatIconButton } from '@angular/material/button';
import { MatTooltip } from '@angular/material/tooltip';
import { Style, ViewField } from '../../../model/ql/view';
import { QLRow } from '../../../model/ql/slug-row';
import { capitalize } from 'jde-spa';

//A host's text for a text cell in place of the row's value, or undefined to show the value:  a group's root by its browse path.  One
//that reads a signal redraws the cell when the signal changes.
export type CellText = ( column:string, row:QLRow )=>string|undefined;

@Component({
	selector: 'ql-table',
	styleUrls: ['./graphql-table.scss'],
	templateUrl: './graphql-table.html',
	imports: [DatePipe, MatCheckbox, MatChipsModule, MatIcon, MatIconButton, MatTableModule, MatSortModule, MatTooltip]
})
export class GraphQLTable{

	checkboxLabel( row?: QLRow ): string{
		return row
			? `${this.selections().isSelected(row) ? 'deselect' : 'select'} row ${row.name}`
			: `${this.isAllSelected() ? 'select' : 'deselect'} all`;
	}
	toggle( row: QLRow ){ this.selections.set( GraphQLTable.toggled(this.selections(), row) ); }
	//`row` ticked or unticked in a new model:  rows, not ids - the row click path, the highlight and ql-list's selection() all key off the row object
	static toggled( selections:SelectionModel<QLRow>, row:QLRow ):SelectionModel<QLRow>{
		const rows = selections.isSelected(row) ? selections.selected.filter( (x)=>x!=row ) : selections.selected.concat( row );
		return new SelectionModel<QLRow>( selections.isMultipleSelection(), rows );
	}

	toggleAll(){
		if( this.isAllSelected() )
			this.selections.set( new SelectionModel<QLRow>(this.selections().isMultipleSelection(), []) );
		else
			this.selections.set( new SelectionModel<QLRow>(this.selections().isMultipleSelection(), [...this.dataSource()]) );
	}

	cellClick( row:QLRow ){
		const isSelected = this.selections().isSelected( row );
		const multi = this.selections().isMultipleSelection();
		let selections = [];
		if( multi )
			selections = isSelected ? this.selections().selected.filter( (x)=>x!=row ) : this.selections().selected.concat( row );
		else
			selections = isSelected ? [] : [row];
		this.selections.set( new SelectionModel<QLRow>(multi, selections) );
		this.onRowActivate.emit( row );
	}

	isAllSelected(){ return this.selections().selected.length==this.dataSource().length; }
	isSelected( row:QLRow ){ return this.selections().isSelected(row); }
	columnName( colName:string ){ return this.columns().get( colName )?.field.displayName ?? capitalize(colName); }
	//Applied to the header cell and the data cell from the one setting, so the two cannot drift apart.
	columnStyle( colName:string ):Record<string,string>{ return this.columns().get( colName )?.style ?? noStyle; }
	objectValue( obj: unknown ): string{
		if( obj==null )
			return '';
		if( typeof obj === 'string' )
			return obj;
		if( typeof obj!=='object' )
			return '';//as before: a scalar has no values to show, and the named/one-value rules below are for objects
		const named = (obj as {name?:unknown}).name;
		if( named!=undefined )
			return `${named}`;
		const values = Object.values( obj ).filter( v=>v!=null );//no `name` - a single remaining property is the display value, e.g. opcSessions{count}.
		return values.length==1 ? `${values[0]}` : '';
	}
	stringValue( column:string, row:QLRow ):unknown{ return this.cellText()?.( column, row ) ?? row[column]; }
	sortable( colName:string ){ return this.columns().get( colName )?.field.type.underlyingKind!=FieldKind.OBJECT; }//orderBy on a grafted object field is a server error.

	//A live-toggle cell (ViewFieldSettings.liveToggle).  Deliberately an icon button and not a mat-slide-toggle:  a slide
	//toggle keeps its own checked state, so a mutation that FAILS would leave the switch showing the state the row never
	//reached.  An icon renders straight off the row, so there is nothing to desync - the cell only ever shows what the
	//server confirmed.
	isLive( column:string, row:QLRow ):boolean{ return row[column]==null; }
	//The verb for the direction a click would take this row - what the tooltip, the aria-label and the confirmation all read.
	liveToggleLabel( column:string, row:QLRow ):string{
		const settings = this.columns().get( column )?.field.liveToggle;
		return (this.isLive(column,row) ? settings?.disable : settings?.enable) ?? "";
	}

	//The tone class for a chip cell.  The column names the tones, the stylesheet owns the colours (_general.scss), so a value the
	//column did not list simply gets the theme's default chip rather than the wrong colour.
	chipClass( colName:string, row:QLRow ):string{
		const tones = this.columns().get( colName )?.field.chip;
		const tone = tones?.[ this.objectValue(row[colName]) ];
		return tone ? `chip-${tone}` : "";
	}

	dataSource=input.required<QLRow[]>();
	displayedFields = input.required<ViewField[]>();
	selections=model.required<SelectionModel<QLRow>>();
	//showDeleted = input<boolean>( false );
	sort = model<Sort>();
	pendingLive = input<unknown[]>( [] );//row ids whose live-toggle mutation is in flight - the switch is disabled until the server answers
	cellText = input<CellText>();
	onSortChange = output<Sort>();
	onRowActivate = output<QLRow>();
	onToggleLive = output<QLRow>();

	displayedColumnNames = computed( ()=>(this.selections().isMultipleSelection() ? ["select"] : []).concat( this.displayedFields().filter((x)=>x.displayed).map((x)=>x.name) ) );
	//each field's cell, classified once per change of fields rather than once per change-detection pass and per cell
	columns = computed( ()=>new Map( this.displayedFields().map( f=>[f.name, {field: f, kind: columnKind( f ), style: cellStyle( f.style )}] ) ) );
	//the matColumnDef lists, one per kind:  a field is in one at most, or its column is declared twice and mat-table throws
	columnNames = computed( ()=>{
		const y:Record<ColumnKind,string[]> = { chip: [], liveToggle: [], string: [], object: [], list: [], date: [], bool: [], uint: [] };
		for( const {field, kind} of this.columns().values() ){
			if( kind )
				y[kind].push( field.name );
		}
		return y;
	} );
}
type ColumnKind = "chip"|"liveToggle"|"string"|"object"|"list"|"date"|"bool"|"uint";
//A chip or a switch column is whatever type it always was - it just renders as a chip or a switch (`deleted` is a DateTime).
function columnKind( x:ViewField ):ColumnKind|undefined{
	const type = x.type;
	if( x.chip )
		return "chip";
	if( x.liveToggle )
		return "liveToggle";
	if( type.underlyingKind==FieldKind.LIST )
		return "list";
	if( type.underlyingKind==FieldKind.OBJECT )
		return "object";
	if( type.underlyingKind==FieldKind.ENUM || (type.underlyingKind==FieldKind.SCALAR && type.underlyingName=="String") )
		return "string";
	if( type.underlyingName=="DateTime" )
		return "date";
	if( type.underlyingName=="Boolean" )
		return "bool";
	if( ["UInt", "ID"].includes(type.underlyingName) )
		return "uint";
	return undefined;
}
//`align` is expanded rather than passed through: it is not a CSS property, and a mat-cell is a flex container - text-align on
//its own leaves the content wherever justify-content put it.
function cellStyle( style:Style|undefined ):Record<string,string>{
	if( !style )
		return noStyle;
	const y:Record<string,string> = {};
	for( const [key,value] of Object.entries(style) ){
		if( value==null )
			continue;
		if( key=="align" ){
			y["justifyContent"] = value=="right" ? "flex-end" : value=="center" ? "center" : "flex-start";
			y["textAlign"] = `${value}`;
		}
		else
			y[key] = `${value}`;
	}
	return y;
}
const noStyle:Record<string,string> = {};