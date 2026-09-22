import { Component, computed, effect, signal, inject } from '@angular/core';
import { SelectionModel } from '@angular/cdk/collections';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';

import { arraysEqual, ChipTones, cloneClassArray, DetailPage, Flex, IGraphQL, Properties, QLSelector, Style, TableSettings, SlugRow, toIdArray, ViewFieldSettings } from 'jde-framework';

import { RolePK } from '../../model/role';
import { Permission } from '../../model/permission';
import { AccessService } from '../../services/access-service';
import { GroupPK } from '../../model/group';
import { User } from '../../model/user';
import { KeyProperties } from './key-properties/key-properties';
import { EffectiveRights } from './effective-rights/effective-rights';

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
	//the server authenticates a logon by loginName+provider, so changing either on an existing user re-binds the account - only a
	//new user sets them.  One list for both forms:  key-properties has no loginName field and ignores it.
	get readonlyFields():string[]{ return this.user?.id ? ["provider", "loginName"] : []; }
	override ql:IGraphQL = inject( AccessService );
}

//the certificate columns key-properties owns:  excluded from the generic form so a Google/password identity does not
//get a Modulus or a Fingerprint field it can never fill in.  Any new cert column belongs here as well as in that form.
const keyFields = ["modulus", "exponent", "issuer", "subjectAlt", "distinguished", "expiration", "fingerprint"];

//the provider as a chip:  the sign-in providers (access_provider_types) in the primary tone, a certificate identity in the
//resting one;  an OpcServer-provided identity shows its server's slug, which no list can name, and gets the default chip
const providerTones:ChipTones = { Google:"ok", Facebook:"ok", Amazon:"ok", Microsoft:"ok", VK:"ok", Key:"neutral" };
const identityColumns:(string|ViewFieldSettings)[] = [ { name:"name", style: new Style(300) }, { name:"provider", style: new Style(100), chip: providerTones } ];
//three system views:  'all' (the default), the people - password/Google logons - and the certificate identities with their key columns
export const userTableSettings:TableSettings = {
	empty: { title: "No users yet.", detail: "An identity becomes a user the first time it signs in.", add: "Use Add to create one ahead of that." },
	excludedColumns: ["isGroup"],
	viewName: "All",
	columns: [ ...identityColumns, "description" ],
	views: [
		{ name: "Users", columns: [ ...identityColumns, "email", "loginName", "description" ], filters: [ {name: "issuer", value: ["<null>"]} ] },
		{ name: "Certs", columns: [ "slug", "modulus", "issuer", "distinguished", "subjectAlt", "expiration", "description" ], sort: "slug", filters: [ {name: "provider", value: ["Key"]} ] }
	]
 }
 //A resource row is the authorizer's per-table switch, not an editable record.  `deleted` is what the switch reads:
 //Authorize::Test bails on a deleted resource ("not enabled"), so a DELETED resource is an UNENFORCED one and the
 //installation ships every row deleted (ResourceSyncAwait creates each one and immediately deletes it) so a fresh system is
 //not locked down.  Hence the shape of this page:  no Add and no detail route (the natural key is schema+slug+criteria,
 //which `:slug` alone cannot address), no Purge - the resources table advertises only Delete and Subscribe as grantable
 //rights, so nobody can even hold Update on it - and the one action that matters, enforcement, as a switch in the row.
 //Enforced leads the columns because it is the only thing on the page that DOES anything;  schemaName ("access",
 //"opc.default", ...) still groups the list, so it leads the sort.
 //One system view, 'Tables':  the rows with no criteria - the per-table switches the page exists for.  A criteria row is a
 //node-scoped resource access_role_add mints when a role is granted on a node; it is managed from the node's Access tab and
 //read on the user's Effective rights tab, and here it only ever looked like a duplicate of its table (same slug, no
 //criteria column, a name that differs by case) - so this page does not list it at all.
 export const resourceTableSettings:TableSettings = {
	empty: { title: "No resources registered.", detail: "Every service registers its tables here when it starts - start the hub or the app server against this database.", icon: "lock" },
	canAdd: false,
	canPurge: false,
	canNavigate: false,//there is no 'resources/:slug' route - resources are server-defined rows with no detail page
	viewName: "Tables",
	filters: [ {name: "criteria", value: ["<null>"]} ],
	columns: [
		{ name:"deleted", displayName:"Enforced", style: new Style({flex: new Flex(110), align: "center"}), liveToggle: {
			enable: "Enforce",
			disable: "Stop enforcing",
			enableMessage: "Access to this table will be restricted to users and roles holding an explicit permission on it. Grant yourself one first, or you may lose your own access.",
			disableMessage: "Permissions on this table will stop being checked and every signed-in user will have full access to it."
		} },
		{ name:"schemaName", displayName:"Schema", style: new Style(150) },
		{ name:"name", style: new Style(300) },
		"description"
	],
	sort: [ {active:"schemaName", direction:"asc"}, {active:"name", direction:"asc"} ]
 }
