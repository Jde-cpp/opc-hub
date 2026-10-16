import { Component, computed, model, Inject, OnDestroy, OnInit, signal, ViewChild, input, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbarModule } from '@angular/material/toolbar';
import { UaNode } from '../../../model/node';
import { NodeRights } from './node-rights/node-rights';
//import { Gateway } from '../../../../services/gateway.service';
import { NodeRoute } from '../../../model/node-route';
import { ProfileStore } from 'jde-spa';
import { AppService, Mutation, MutationType, SnackbarService } from 'jde-framework';
import { ActivatedRoute } from '@angular/router';
import { Permission, Rights, Role } from 'jde-access';

export type RolePermission = {
	roleId: number;
	roleName: string;
	permissionId: number;
	allowed:number;
	denied:number;
	resourceId: number;
	criteria: string;
};
@Component( {
	selector: 'node-access',
	templateUrl: './node-access.html',
	styleUrls: ['./node-access.scss'],
	host: {class:'main-content mat-drawer-container my-content'},
	imports: [CommonModule, MatTabsModule, MatToolbarModule, MatButtonModule, NodeRights]
})
export class NodeAccess implements OnInit, OnDestroy{
	private actRoute:ActivatedRoute = inject( ActivatedRoute );

	async ngOnInit(): Promise<void> {
		this.actRoute.data.subscribe( async (data)=>await this.load() );
	}

	private async load():Promise<void>{
		const criteria = this.node().nodeId.uaString();//e.g. "ns=4;i=6020" — the server's resource criteria for this node
		const vars = { schemaName: `opc.${this.accessResource()}`, slug: "nodeIds", criteria };
		const q = 'roles(deleted:null){ id name permissionRight{ id allowed denied resource(schemaName:$schemaName, criteria:$criteria, slug:$slug){ id criteria } } }';
		const db = await this.appService.queryArray<any>( q, vars, (m)=>console.log(m) );
		const roles:RolePermission[] = db.map( (role:any)=>{
			const pr = role.permissionRight;//present only where a permission exists for this exact node; allowed/denied are [Right] name arrays
			return {
				roleId: role.id,
				roleName: role.name,
				permissionId: pr?.id ?? 0,
				allowed: Permission.toRights( pr?.allowed ?? [] ),
				denied: Permission.toRights( pr?.denied ?? [] ),
				resourceId: pr?.resource?.id ?? 0,
				criteria: pr?.resource?.criteria ?? criteria,
			};
		}).sort( (a,b)=>a.roleName.localeCompare(b.roleName) );
		this.original = roles;
		this.roles = roles.map( r=>({...r}) );
		this.isLoading.set( false );
	}

	ngOnDestroy() {
		ProfileStore.setTabIndex( 'nodeAccess', this.tabIndex );
  }
	async save(){
		const mutations = this.mutations();
		if( !mutations.length )
			return;
		try{
			await this.appService.mutate( mutations, (m)=>console.log(m) );
		}
		catch( e ){
			this.snackbar.exception( "Could not save node access.", e );//a failed save used to be indistinguishable from a successful one - the rejection just escaped
			return;//keep mutations() so Save stays enabled for a retry, and leave the baseline alone - nothing was applied
		}
		this.mutations.set( [] );
		//Re-query rather than copying `roles` over `original`: the pending batch is now applied, so every later toggle must
		//diff against the saved state (it used to re-Add rights the server already had, and a second Save resent the lot),
		//and a newly Added permission only gets its permissionId/resourceId from the server - without them the next Remove
		//would send `permissionRight:{id:0}` and the next Add would recreate the resource instead of reusing it.
		await this.load();
	}
	onTabIndexChanged( index:number ){ this.tabIndex = index; }
	onToggle( event:{ role: RolePermission, rights:number } ):void{
		const role = event.role;
		toggleRights( role, event.rights );
		const original = this.original.find( x=>x.roleId==role.roleId )!;
		const mutation = roleMutation( role, original, ()=>{
			const resource:Record<string,unknown> = role.resourceId ? { id: role.resourceId } : { schemaName: `opc.${this.accessResource()}`, slug: "nodeIds" };//was `schema:` — server ignored it and stored schemaName as "opc", putting the permission on the wrong resource
			resource["criteria"] = role.criteria ? role.criteria : null;
			return resource;
		} );
		this.mutations.set( withMutation( this.mutations(), role.roleId, mutation ) );
		if( mutation )
			console.log( mutation.toString() );
	}

	access = Rights;//node ACLs use the generic Right enum (Create/Read/Update/Delete/Purge/Administer/Subscribe/Execute), same as the rest of access — NOT EAccess/EWriteAccess
	mutations = signal<Mutation[]>( [] );
	isLoading = signal( true );
	node = model.required<UaNode>();
	tabIndex:number = ProfileStore.tabIndex( 'nodeAccess' );
	original!:RolePermission[];

	accessResource = input.required<string>();
	route = model.required<NodeRoute>();
	roles: RolePermission[]=[];
	appService = inject(AppService);
	snackbar = inject(SnackbarService);
}

//A click on a right's cell, as the grid cycles it:  allowed to denied, denied to none, none to allowed;  the None column (rights 0)
//clears both sides.  Shared with a historian group's Access tab (hist-group-access.ts), which grants on the same grid.
export function toggleRights( role:RolePermission, rights:number ):void{
	if( NodeRights.isAllowed(role, rights) ){//allowed->denied
		role.allowed &= ~rights;
		role.denied |= rights;
	}
	else if( NodeRights.isDenied(role, rights) )//denied->none
		role.denied &= ~rights;
	else if( rights==0 ){//none->allowed
		role.allowed = 0;
		role.denied = 0;
	}
	else
		role.allowed |= rights;
}
//The mutation that takes a role from the server's row to its edited one, or none where the row is the server's again.  Remove-vs-Add is
//decided from the role's RESULTING rights, never from the side that changed:  clearing one half of a two-sided permission (allowed=Read,
//denied=Write -> clear the denied Write) used to look like "no rights left" and removed the whole permissionRight, silently dropping the
//still-wanted allowed=Read.  Permission.roleMutations evaluates the resulting row; match it.  `resource` names the row an Add grants on.
export function roleMutation( role:RolePermission, original:RolePermission, resource:()=>Record<string,unknown> ):Mutation|undefined{
	if( role.allowed==original.allowed && role.denied==original.denied )
		return undefined;
	if( !role.allowed && !role.denied )
		return role.permissionId ? new Mutation( Role.typeName, role.roleId, {permissionRight: {id: role.permissionId}}, MutationType.Remove ) : undefined;//permissionId 0 = nothing on the server to remove (a pending Add is dropped by returning none)
	return new Mutation( Role.typeName, role.roleId, {permissionRight: {allowed: role.allowed, denied: role.denied, resource: resource()}}, MutationType.Add );
}
//the pending batch with `mutation` in the place of the role's earlier one, or without one where the role is back to the server's row
export function withMutation( mutations:Mutation[], roleId:number, mutation:Mutation|undefined ):Mutation[]{
	const index = mutations.findIndex( m=>m.id==roleId );
	if( index<0 )
		return mutation ? [...mutations, mutation] : mutations;
	const y = mutations.slice();
	if( mutation )
		y[index] = mutation;
	else
		y.splice( index, 1 );
	return y;
}