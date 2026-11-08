import { Component, computed, input } from '@angular/core';
import { MatTooltipModule } from '@angular/material/tooltip';
import { ExtensionObject, isStructure, isStructures, Structure, Value, valueString } from '../../model/value';

//A structure in a value cell (clients spec *Structures*, #422):  a table of its members by name, in the type's order, a member that is
//a structure a table of its own inside it, an array of structures a table per element, a body no type explains said to be undecoded with
//why in a tooltip a keyboard reaches too, and any other member as valueString spells it.  The write dialog edits a structure member by
//member (ValueEditor, #423).  The Children tab's Snapshot, the Attributes tab's Value, the History table's Value and the watch list's
//Value cells place one where StructureTable.shows( value ), their own text otherwise.
export type StructureRow = { name:string; value:Value|null; nested:boolean; text?:string };
@Component({
	selector: 'structure-table',
	templateUrl: './structure-table.html',
	styleUrl: './structure-table.scss',
	imports: [MatTooltipModule],
	host: { '[class.top]': '!nested()' }
})
export class StructureTable{
	value = input.required<Value|null|undefined>();
	label = input<string>();//what the table is of, for its accessible name:  the variable's name
	nested = input( false );//a member's, or an array's element:  inside another, not the cell's own
	structure = computed<Structure|undefined>( ()=>{ const v = this.value(); return isStructure( v ) ? v : undefined; } );
	structures = computed<Structure[]|undefined>( ()=>{ const v = this.value(); return isStructures( v ) ? v : undefined; } );
	undecoded = computed<ExtensionObject|undefined>( ()=>{ const v = this.value(); return v instanceof ExtensionObject ? v : undefined; } );
	rows = computed<StructureRow[]>( ()=>Object.entries( this.structure() ?? {} ).map( ([name, value])=>StructureTable.row( name, value ) ) );
	static shows( value:unknown ):boolean{ return isStructure( value ) || isStructures( value ) || value instanceof ExtensionObject; }
	static row( name:string, value:Value|null ):StructureRow{
		return StructureTable.shows( value ) ? { name, value, nested: true } : { name, value, nested: false, text: value===null ? "" : valueString( value ) };
	}
}