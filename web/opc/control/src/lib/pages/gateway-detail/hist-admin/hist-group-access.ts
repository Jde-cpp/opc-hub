import { Component, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { RouterLink } from '@angular/router';
import { Permission } from 'jde-access';
import { AppService, confirm, errorText, Mutation, MutationType, SnackbarService } from 'jde-framework';
import { RolePermission, roleMutation, toggleRights, withMutation } from '../../node-detail/node-access/node-access';
import { NodeRights } from '../../node-detail/node-access/node-rights/node-rights';

//The group as the tab needs it:  its id, the criteria of its own resource, and its name, what that resource is called
export type HistGroupRef = { id:number; name:string };
//A resource row as the tab reads it, the group's own or the table's;  `enforced` is `deleted` null, the Resources page's switch
export type HistGroupResource = { id:number; name:string; enforced:boolean };
//Which row governs the group (spec *Authorization*):  its own when it has one enforced, else the table's when that is, else nothing -
//reads pass and edits are refused
export type HistGroupGovernor = 'own'|'table'|'none';
export type HistGroupAccessStatus = { kind:HistGroupGovernor; icon:string; title:string; detail:string };
//the hist_groups table's resource, as the gateway tests it:  Acl()->Test( "gateway", "histGroups", <group id>, … ), the schema shared by
//every gateway instance
export const histGroupResourceKey = { schemaName: 'gateway', slug: 'histGroups' };
//the rights the gate asks for:  Read for history and a subscription, Update for createHistory, updateHistory, upsertHistory and
//resolveHistory, Delete for purgeHistory;  None clears a row, as the node grid's does
export const histGroupRights = { 0: 'None', 2: 'Read', 4: 'Update', 8: 'Delete' };

//A group's Access tab (#339):  the node page's grid of roles against rights on the group's own access_resources row, criteria its id, with
//which row governs it - the hist_groups table's row is the slug's switch, then the group's own where it has one enforced, else the
//table's (status below) - and the row's making and enforcement.  A grant saved on a group with no row of
//its own mints one, enforced, as a node's first grant does (access_role_add);  Make own resource makes one unenforced, for grants to go
//on before it takes over, and Enforce and Stop enforcing are the Resources page's switch on that row.  Everything goes through the hub's
//access schema, as the node tab's does, not the gateway.
@Component({
	selector: 'hist-group-access',
	templateUrl: './hist-group-access.html',
	styleUrls: ['./hist-admin-shared.scss', './hist-group-access.scss'],
	imports: [MatButtonModule, MatIconModule, MatProgressBarModule, MatTooltipModule, RouterLink, NodeRights]
})
export class HistGroupAccess{
	constructor(){
		effect( ()=>{//the router reuses the page from one group to the next:  read the one it holds now
			const group = this.group();
			untracked( ()=>this.load( group ) );
		} );
	}
	group = input.required<HistGroupRef>();
	readonly rights = histGroupRights;
	own = signal<HistGroupResource|undefined>( undefined );
	table = signal<HistGroupResource|undefined>( undefined );
	roles = signal<RolePermission[]>( [] );
	original:RolePermission[] = [];
	mutations = signal<Mutation[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	static readonly resourcesQuery = 'resources( schemaName:$schemaName, slug:$slug ){ id name criteria deleted }';
	static readonly rolesQuery = 'roles( deleted:null ){ id name permissionRight{ id allowed denied resource( schemaName:$schemaName, criteria:$criteria, slug:$slug ){ id criteria } } }';
	//the table's rows, its own and the table's among them, and every live role with its permission on the group's own row where it has one
	async load( group:HistGroupRef=this.group() ):Promise<void>{
		const criteria = String( group.id );
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const [rows, roles] = await Promise.all( [
				this.appService.queryArray<any>( HistGroupAccess.resourcesQuery, histGroupResourceKey, m=>console.log( m ) ),
				this.appService.queryArray<any>( HistGroupAccess.rolesQuery, {...histGroupResourceKey, criteria}, m=>console.log( m ) )
			] );
			if( this.group().id!=group.id )//another group opened meanwhile
				return;
			const toResource = ( r:any ):HistGroupResource|undefined=>r ? { id: Number( r.id ), name: String( r.name ?? '' ), enforced: r.deleted==null } : undefined;
			this.own.set( toResource( rows.find( r=>r.criteria==criteria ) ) );
			this.table.set( toResource( rows.find( r=>r.criteria==null ) ) );
			const permissions:RolePermission[] = roles.map( ( role:any )=>{
				const pr = role.permissionRight;//present only where the role holds a permission on this group's own row;  allowed/denied are [Right] name arrays
				return { roleId: role.id, roleName: role.name, permissionId: pr?.id ?? 0, allowed: Permission.toRights( pr?.allowed ?? [] ), denied: Permission.toRights( pr?.denied ?? [] ), resourceId: pr?.resource?.id ?? 0, criteria };
			} ).sort( ( a, b )=>a.roleName.localeCompare( b.roleName ) );
			this.original = permissions;
			this.roles.set( permissions.map( r=>({...r}) ) );
			this.mutations.set( [] );
		}
		catch( e ){
			this.error.set( errorText( e ) ?? 'Unknown error' );
		}
		finally{
			this.loading.set( false );
		}
	}
	//Which row governs the group, as the hub's Authorize resolves it (FindActiveResourcePKLocked):  the table's row is the slug's switch,
	//consulted first - while it is unenforced no criteria row of the slug is looked at, whatever its own state - then the group's own row
	//where it has one enforced, else the table's;  and what a grant here does about it.
	status = computed<HistGroupAccessStatus>( ()=>{
		const own = this.own(), table = this.table(), name = this.group().name;
		if( !table?.enforced ){
			const next = own
				? own.enforced ? "The group's own resource is enforced already, so the grants here apply the moment the table's is." : "The group's own resource isn't enforced;  once both are, the grants here apply."
				: "A grant saved here makes the group's own resource, enforced, so the grants here apply the moment the table's is;  Make own resource makes one that isn't enforced yet.";
			return { kind: 'none', icon: 'remove_moderator', title: 'Nothing enforces it.', detail: `Anyone signed in reads ${name}'s history, and every edit is refused:  the hist_groups table's resource isn't enforced, and its switch, on the Resources page, is what turns checking on for the groups' own rows too.  ${next}` };
		}
		if( own?.enforced )
			return { kind: 'own', icon: 'verified_user', title: 'Governed by its own resource, enforced.', detail: `Only the roles granted here read ${name}'s history, update it or delete it, whatever they hold on the hist_groups table.` };
		const next = own ? "The group's own resource isn't enforced yet:  Enforce it and the grants here take over." : "A grant saved here makes the group's own resource, enforced, and the grants here take over;  Make own resource makes one that isn't enforced yet.";
		return { kind: 'table', icon: 'shield', title: "Governed by the hist_groups table's resource, enforced.", detail: `The table's grants apply to every group without an enforced row of its own.  ${next}` };
	} );

	onToggle( event:{ role:RolePermission; rights:number } ):void{
		const role = event.role;
		toggleRights( role, event.rights );
		const original = this.original.find( r=>r.roleId==role.roleId )!;
		//By key, never by id:  access_role_add finds the row by it whether or not the authorizer's cache holds the row, and makes it,
		//enforced and named for the group, where there is none.
		const mutation = roleMutation( role, original, ()=>({ ...histGroupResourceKey, criteria: role.criteria, name: this.group().name }) );
		this.mutations.set( withMutation( this.mutations(), role.roleId, mutation ) );
		if( mutation )
			console.log( mutation.toString() );
	}
	async save():Promise<void>{
		const mutations = this.mutations();
		if( !mutations.length )
			return;
		this.busy.set( true );
		try{
			await this.appService.mutate( mutations, m=>console.log( m ) );
		}
		catch( e ){
			this.snackbar.exception( "Could not save the group's access.", e );//the batch stays, so Save retries it
			return;
		}
		finally{
			this.busy.set( false );
		}
		await this.load();//the permission ids the server gave, and the row a first grant minted
	}
	//A resource of the group's own, made unenforced as a service's sync makes a table's (ResourceMAwait):  the table's row, or nothing,
	//governs the group until Enforce.  Needs Administer on the gateway schema's enforced roots, if any.
	async makeResource():Promise<void>{
		const group = this.group();
		this.busy.set( true );
		try{
			await this.appService.mutate( new Mutation( 'Resource', 0, {...histGroupResourceKey, criteria: String( group.id ), name: group.name}, MutationType.Create, ['id'] ), m=>console.log( m ) );
			this.snackbar.info( `${group.name} has a resource of its own, not enforced yet.` );
		}
		catch( e ){
			this.snackbar.exception( "Could not make the group's resource.", e );
		}
		finally{
			this.busy.set( false );
		}
		await this.load();
	}
	//the Resources page's switch on the group's own row, confirmed both ways as there:  enforcing can lock the operator out of the group,
	//and stopping opens it to whatever the table's row allows
	async setEnforced( enforce:boolean ):Promise<void>{
		const own = this.own();
		if( !own )
			return;
		const name = this.group().name;
		const verb = enforce ? 'Enforce' : 'Stop enforcing';
		const pending = this.table()?.enforced ? '' : "  Nothing is checked until the hist_groups table's resource is enforced as well, on the Resources page.";
		const message = enforce
			? `Only the roles granted here will read ${name}'s history, update it or delete it, whatever they hold on the hist_groups table.  Grant yourself one first, or you may lose your own access.${pending}`
			: `${name} goes back under the hist_groups table's resource:  its grants apply where it is enforced;  where it isn't, anyone signed in reads the history and every edit is refused.`;
		if( !await confirm( this.dialog, {title: `${verb} ${name}?`, message, confirm: verb} ) )
			return;
		this.busy.set( true );
		try{
			await this.appService.mutate( `${enforce ? 'restore' : 'delete'}Resource( id:${own.id} )`, m=>console.log( m ) );
		}
		catch( e ){
			this.snackbar.exception( `${verb} failed.`, e );
		}
		finally{
			this.busy.set( false );
		}
		await this.load();
	}
	appService = inject( AppService );
	snackbar = inject( SnackbarService );
	dialog = inject( MatDialog );
}