import { Component, computed, input } from '@angular/core';
import { KeyValue } from '@angular/common';
import { MatIcon } from '@angular/material/icon';
import { MatTableModule } from '@angular/material/table';
import { MatTooltip } from '@angular/material/tooltip';
import { RouterLink } from '@angular/router';
import { HolderKind, ResourceHolder } from '../../../model/resource-holder';

//One tab of the resource page:  the roles, groups or users a resource's grants reach, one column per right the resource offers.
//EffectiveRights' marks - a deny wins, hover a mark for the grants behind it - turned around:  a row is a holder, not a resource.
@Component({
	selector: 'holder-table',
	templateUrl: 'holder-table.html',
	styleUrls: ['holder-table.scss'],
	imports: [MatIcon, MatTableModule, MatTooltip, RouterLink]
})
export class HolderTable{
	kind = input.required<HolderKind>();
	rows = input.required<ResourceHolder[]>();
	rights = input.required<KeyValue<number,string>[]>();
	link = input.required<string>();//the holders' list, '/access/users' - a row with a slug links to its page under it
	empty = input.required<string>();
	nameHeader = computed<string>( ()=>({ role: "Role", group: "Group", user: "User" })[this.kind()] );
	displayedColumns = computed<string[]>( ()=>[ "name", ...this.rights().map( kv=>kv.value ) ] );
}