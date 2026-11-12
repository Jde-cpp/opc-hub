import { Component, input, output } from '@angular/core';
import { MatChip } from '@angular/material/chips';
import { MatIcon } from '@angular/material/icon';
import { GroupHeader } from '../../model/ql/group';

//A group's header (views spec *Group by*), the one cell of its row, the same in ql-table and the hand-built tables:  the chevron
//that collapses the group, the value as its column reads - a chip where the column is one, *No <field>* for the rows with none -
//the count of its rows here, *of* the list's where that is more, and the sums chosen.  The table gives the cell a column of its
//own and a row definition `when` the row is a header (isGroupHeader);  the row's look is `.jde-group-row`'s (_general.scss).
@Component({
	selector: 'mat-cell[groupHeader]',
	templateUrl: './group-header.html',
	styleUrl: './group-header.scss',
	imports: [MatChip, MatIcon],
	host: { class: 'jde-group-cell' }
})
export class GroupHeaderCell{
	header = input.required<GroupHeader>( {alias: "groupHeader"} );
	groupToggle = output<void>();//the chevron:  the host collapses the group in its views (ViewSet.toggleGroup)
}