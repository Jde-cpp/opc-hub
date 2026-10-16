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
import { AppService, Mutation, SnackbarService } from 'jde-framework';
import { ActivatedRoute } from '@angular/router';
import { Rights } from 'jde-access';
import { RolePermission, RolePermissionRow, roleMutation, toggleRights, toRolePermissions, withMutation } from './node-rights/role-permission';

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
		const roles = toRolePermissions( await this.appService.queryArray<RolePermissionRow>( q, vars, (m)=>console.log(m) ), criteria );//a permission only where one exists for this exact node
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