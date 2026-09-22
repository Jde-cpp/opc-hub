import { Component, effect, signal, inject } from '@angular/core';
import { SelectionModel } from '@angular/cdk/collections';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltip } from '@angular/material/tooltip';

import { arraysEqual, cloneClassArray, DetailPage, Properties, IGraphQL, QLSelector, Style, toIdArray, SlugRow} from 'jde-framework';

import { RolePK } from '../../model/role';
import { Permission } from '../../model/permission';
import { AccessService } from '../../services/access-service';
import { Group, GroupPK } from '../../model/group';
import { UserPK } from '../../model/user';

@Component( {
    templateUrl: './group-detail.html',
		styleUrls: ['./group-detail.scss'],
		//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content mat-drawer-container my-content group-detail'},
    imports: [CommonModule, MatButtonModule, MatIcon, MatTabsModule, MatTooltip, Properties, QLSelector]
})
export class GroupDetail extends DetailPage<Group>{
	constructor(){
		super( 'groupDetail' );
		effect(() => {
			if( this.users() && !arraysEqual(SlugRow.idArray(this.row.users ?? []),this.users().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.childGroups() && !arraysEqual(SlugRow.idArray(this.row.childGroups ?? []),this.childGroups().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.roles() && !arraysEqual(SlugRow.idArray(this.row.roles), this.roles().selected) )
				this.isChanged.set( true );
		});
	}

	protected override get ctor(){ return Group; }
	protected override onRow(){
		this.users.set( new SelectionModel<UserPK>(true, SlugRow.idArray(this.row.users)) );
		this.childGroups.set( new SelectionModel<GroupPK>(true, SlugRow.idArray(this.row.childGroups)) );
		this.permissions.set( cloneClassArray(this.row.permissions, Permission) );
		this.roles.set( new SelectionModel<RolePK>(true, SlugRow.idArray(this.row.roles)) );
	}
	protected override upsert():Group{
		return new Group( {id:this.properties().id, ...this.properties(), permissions: this.permissions(), users: this.users().selected, roles: this.roles().selected, childGroups: toIdArray(this.childGroups().selected)} );
	}

	get group(){ return this.row; }//the template's name for it
	users = signal<SelectionModel<UserPK>>( null as any );
	childGroups = signal<SelectionModel<GroupPK>>( null as any );
	roles = signal<SelectionModel<RolePK>>( null as any );
	permissions = signal<Permission[]>( null as any );//the row's direct acl grants, carried through upsert unedited - the Permissions tab is hidden for MVP (see the template)
	excludedColumns = [...groupTableSettings.excludedColumns, "provider"];//a group has no login provider - the column exists only because groups share the identity view with users.
	override ql:IGraphQL = inject( AccessService );
}
export const groupTableSettings = {
	empty: { title: "No groups yet.", detail: "A group collects users so a role is granted once for all of them.", add: "Use Add to create one." },
	excludedColumns: ["isGroup", "members"],
	columns: [ //without this the list falls back to ListRoute's name/created/updated/deleted/slug default.
		{ name:"name", style: new Style(300) },
		"slug",
		"description"
	],
	collectionName: "groups"
}
