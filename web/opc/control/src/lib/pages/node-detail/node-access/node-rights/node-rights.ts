import { Component, input, output, effect, viewChild } from '@angular/core';
import { KeyValue } from '@angular/common';
import { MatCheckboxModule } from "@angular/material/checkbox";
import { MatTable, MatTableModule } from "@angular/material/table";

import { MatSortModule, Sort } from "@angular/material/sort";
import { MatTooltipModule } from "@angular/material/tooltip";
import { isAllowed, isDenied, RolePermission } from './role-permission';

@Component( {
	selector: 'node-rights',
	templateUrl: './node-rights.html',
	host: {class:'main-content mat-drawer-container my-content'},
	imports: [MatSortModule, MatTableModule, MatCheckboxModule, MatTooltipModule]
})
export class NodeRights {
	constructor(){
		effect( ()=>{
			this.data = this.roles().slice();
			this.sortData( this.sort );
		});
	}

	static readonly isDenied = isDenied;//the template's, through `self`
	static readonly isAllowed = isAllowed;
	toggle( role: RolePermission, rights:number ):void{
		if( this.disabled() )//the denied icon is a plain span, which nothing else stops
			return;
		this.toggleEmitter.emit( { role: role, rights: rights } );
	}
	//Every header carries mat-sort-header, but only 'roleName' is a field on the row:  the rights columns are named after the
	//RIGHT (available.value) and 'inherited' was not on RolePermission at all, so `a[colName]` was undefined and localeCompare
	//threw on every header but Role.  Sort a rights column by what the row shows in it, and keep roleName as the tiebreak so
	//the order inside a group stays the alphabetical one.
	sortData( $event:Sort ){
		const colName = $event.active;
		const right = this.available.find( kv=>kv.value==colName );
		const rank = ( role:RolePermission ):number=>
			right ? (NodeRights.isAllowed(role, right.key) ? 0 : NodeRights.isDenied(role, right.key) ? 1 : 2)
			: role.inherited ? 0 : 1;//unset where the owner fills none, as NodeAccess doesn't
		const compare = colName=="roleName"
			? ( a:RolePermission, b:RolePermission )=>a.roleName.localeCompare( b.roleName )
			: ( a:RolePermission, b:RolePermission )=>rank(a)-rank(b) || a.roleName.localeCompare( b.roleName );
		this.data = this.data.slice().sort( (a,b)=>$event.direction=="asc" ? compare(a,b) : -compare(a,b) );
		this.table()?.renderRows();
	}
	get available(): KeyValue<number,string>[] {
		let y:KeyValue<number,string>[] = [];
		for( let right in this.rights() ){
			let key = Number(right);
			if( !isNaN(key) )
				y.push( { key: key, value: this.rights()[right] } );
		}
		return y;
	}
	get displayedColumnNames(): string[] {
		let y = ['roleName', 'inherited'];
		for( let kv of this.available )
			y.push( kv.value );
		return y;
	}

	toggleEmitter = output<{role: RolePermission, rights:number}>(); //
	rights = input<any>();
	disabled = input( false );//while the owner's rows are being read or sent:  a tick then would be dropped, or act on rows about to change
	self = NodeRights;
	roles = input.required<RolePermission[]>();
	data:RolePermission[] = [];
	get sort():Sort{ return {active: "roleName", direction: "asc"}; }
	table = viewChild<MatTable<RolePermission>>( 'mainTable' );
}