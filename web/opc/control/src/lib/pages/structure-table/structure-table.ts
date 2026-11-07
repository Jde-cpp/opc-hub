import { Component, computed, input } from '@angular/core';
import { MatTooltipModule } from '@angular/material/tooltip';
import { ExtensionObject, isStructure, Structure, Value, valueString } from '../../model/value';

//A structure in a value cell (clients spec *Structures*, #422):  a nested table of its members by name, in the type's order, a member that
//is a structure a table of its own inside it, an array of structures one per element, and any other member as valueString spells it.  Read
//only, until the write dialog edits a structure member by member (clients plan, Phase 9).  The Children tab's Snapshot, the History
//table's Value and the watch list's Value cells render one through it.
export type StructureRow = { name:string; value:Value|null; structure?:Structure; structures?:Structure[]; text?:string; tooltip?:string };
@Component({
	selector: 'structure-table',
	templateUrl: './structure-table.html',
	styleUrl: './structure-table.scss',
	imports: [MatTooltipModule]
})
export class StructureTable{
	members = input.required<Structure>();
	label = input<string>();//what the table is of, for its accessible name:  the variable's name
	rows = computed<StructureRow[]>( ()=>Object.entries( this.members() ).map( ([name, value])=>StructureTable.row( name, value ) ) );
	static row( name:string, value:Value|null ):StructureRow{
		if( isStructure(value) )
			return { name, value, structure: value };
		if( Array.isArray(value) && value.length && value.every( isStructure ) )
			return { name, value, structures: <Structure[]>value };
		if( value instanceof ExtensionObject )
			return { name, value, text: value.toString(), tooltip: value.tooltip };
		return { name, value, text: value===null ? "" : valueString( value ) };
	}
}