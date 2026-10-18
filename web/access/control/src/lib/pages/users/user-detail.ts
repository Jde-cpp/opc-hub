import { Component, computed, effect, signal, inject } from '@angular/core';
import { SelectionModel } from '@angular/cdk/collections';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';

import { arraysEqual, cloneClassArray, DetailPage, IGraphQL, Properties, QLSelector, SlugRow, toIdArray } from 'jde-framework';

import { RolePK } from '../../model/role';
import { Permission } from '../../model/permission';
import { AccessService } from '../../services/access-service';
import { GroupPK } from '../../model/group';
import { User } from '../../model/user';
import { KeyProperties } from './key-properties/key-properties';
import { EffectiveRights } from './effective-rights/effective-rights';
import { userTableSettings } from './user-table-settings';

@Component( {
    templateUrl: './user-detail.html',
		styleUrls: ['./user-detail.scss'],
		//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content mat-drawer-container my-content user-detail'},
    imports: [CommonModule, MatButtonModule, MatIcon, MatTabsModule, Properties, KeyProperties, QLSelector, EffectiveRights]
})
export class UserDetail extends DetailPage<User>{
	constructor(){
		super( 'userDetail' );
		effect(() => {
			if( this.groups() && !arraysEqual(SlugRow.idArray(this.row.groups ?? []),this.groups().selected) )
				this.isChanged.set( true );
		});
		effect(() => {
			if( this.roles() && !arraysEqual(SlugRow.idArray(this.row.roles), this.roles().selected) )
				this.isChanged.set( true );
		});
	}

	protected override get ctor(){ return User; }
	protected override onRow(){
		this.groups.set( new SelectionModel<GroupPK>(true, SlugRow.idArray(this.row.groups)) );
		this.permissions.set( cloneClassArray(this.row.permissions ?? [], Permission) );
		this.roles.set( new SelectionModel<RolePK>(true, SlugRow.idArray(this.row.roles ?? [])) );
	}
	protected override upsert():User{
		return new User( { ...this.properties(), permissions: this.permissions(), roles: this.roles().selected, groups: toIdArray(this.groups().selected) } );
	}

	public copy( existing:User ):User{
		return new User( existing );
	}

	get user(){ return this.row; }//the template's name for it
	groups = signal<SelectionModel<GroupPK>>( null as any );
	permissions = signal<Permission[]>( null as any );//the row's direct acl grants, carried through upsert unedited - the Permissions tab is hidden for MVP (see the template)
	roles = signal<SelectionModel<RolePK>>( null as any );

	userTableSettings = userTableSettings;
	providerName = computed<string>( ()=>{
		const value = this.properties()?.provider as string|number|undefined;
		return typeof value=="number"
			? this.schema.enums.get( "Provider" )?.find( (o)=>o.id==value )?.name ?? ""
			: value ?? "";
	});
	isKeyProvider = computed<boolean>( ()=>this.providerName().toLowerCase()=="key" );
	excludedColumns = [...userTableSettings.excludedColumns!, ...keyFields];
	fieldOrder = ["slug", "name", "provider", "email", "loginName", "description"];//identity together, the provider beside the id it prefixes, the free text last - key-properties keeps the same shape
	//the server authenticates a logon by loginName+provider, so changing either re-binds the account - and a user is only ever
	//created by signing in so neither is ever set here.  One list for both forms:  key-properties has
	//no loginName field and ignores it.
	readonlyFields = ["provider", "loginName"];
	override ql:IGraphQL = inject( AccessService );
}

//the certificate columns key-properties owns:  excluded from the generic form so a Google/password identity does not
//get a Modulus or a Fingerprint field it can never fill in.  Any new cert column belongs here as well as in that form.
const keyFields = ["modulus", "exponent", "issuer", "subjectAlt", "distinguished", "expiration", "fingerprint"];