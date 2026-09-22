import { Component, effect, signal, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { SelectionModel } from '@angular/cdk/collections';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';

import { arraysEqual, cloneClassArray, DetailPage, IGraphQL, Properties, QLSelector, TableSettings, SlugRow, toIdArray, Style} from 'jde-framework';
import { Role, RolePK } from '../../model/role';
import { PermissionTable } from '../../shared/permissions/permission-table';
import { Permission } from '../../model/permission';
import { AccessService } from '../../services/access-service';
import { GroupPK } from '../../model/group';
import { UserPK } from '../../model/user';

@Component( {
    templateUrl: './role-detail.html',
		styleUrls: ['./role-detail.scss'],
		//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content mat-drawer-container my-content role-detail'},
    imports: [CommonModule, MatButtonModule, MatIcon, MatTabsModule, Properties, PermissionTable, QLSelector]
})
export class RoleDetail extends DetailPage<Role>{
	constructor(){
		super( 'roleDetail' );
		effect(() => {
			if( this.childRoles() && !arraysEqual(SlugRow.idArray(this.row.childRoles), this.childRoles().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.groups() && !arraysEqual(SlugRow.idArray(this.row.groups),this.groups().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.users() && !arraysEqual(SlugRow.idArray(this.row.users),this.users().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.permissions() && !Permission.arraysEqual(this.row.permissions, this.permissions()) )
				this.isChanged.set( true );
		});
	}

	protected override get ctor(){ return Role; }
	protected override onRow(){
		this.permissions.set( cloneClassArray(this.row.permissions, Permission) );
		this.childRoles.set( new SelectionModel<RolePK>(true, SlugRow.idArray(this.row.childRoles)) );
		this.groups.set( new SelectionModel<GroupPK>(true, SlugRow.idArray(this.row.groups)) );
		this.users.set( new SelectionModel<UserPK>(true, SlugRow.idArray(this.row.users)) );
	}
	protected override upsert():Role{
		return new Role( {
			id:this.properties().id,
			...this.properties(),
			permissions: this.permissions(),
			roles: this.childRoles().selected,
			groups: toIdArray(this.groups().selected),
			users: toIdArray(this.users().selected)
		});
	}

	public copy( existing:Role ):Role{
		return new Role( existing );
	}

	get role(){ return this.row; }//the template's name for it
	permissions = signal<Permission[]>( null as any );
	childRoles = signal<SelectionModel<RolePK>>( null as any );
	groups = signal<SelectionModel<RolePK>>( null as any );
	users = signal<SelectionModel<RolePK>>( null as any );
	override ql:IGraphQL = inject( AccessService );
}
export const roleTableSettings:TableSettings = {
	empty: { title: "No roles yet.", detail: "An install seeds Viewer, System Administrator, Owner, Engineer, Operator and Maintenance Technician - an empty list means that seed has not run against this database.", add: "Use Add to create one." },
	excludedColumns:["permissions"],
	columns: [
		{ name:"name", style: new Style(300) },
		"description"
	]
}
