import { Component, effect, inject, input, model, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbarModule } from '@angular/material/toolbar';
import { UaNode } from '../../../model/node';
import { NodeRights } from './node-rights/node-rights';
import { Latest, persistedTabIndex } from 'jde-spa';
import { AppService, SnackbarService } from 'jde-framework';
import { Rights } from 'jde-access';
import { RoleGrants, RolePermission, RolePermissionRow, rolePermissionsQuery, toRolePermissions } from './node-rights/role-permission';

@Component( {
	selector: 'node-access',
	templateUrl: './node-access.html',
	host: {class:'main-content my-content'},
	imports: [MatTabsModule, MatToolbarModule, MatButtonModule, NodeRights]
})
export class NodeAccess{
	constructor(){
		effect( ()=>{//the router reuses the page from one node to the next:  read the one it holds now
			this.node();
			untracked( ()=>this.load() );
		} );
	}

	//Latest wins, and another node's rows go before its read:  each row carries the criteria an Add grants on, so a row of the last node's
	//left on screen - while the read is out, after it fails, or landing late over this one's - granted on a node the page no longer shows
	//(reviews-issue/web-refactor-review.md #1).  A failed read leaves no rows:  a toggle has no baseline to diff against.
	private async load():Promise<void>{
		const read = this.#reads.next();
		const criteria = this.node().nodeId.uaString();//e.g. "ns=4;i=6020" — the server's resource criteria for this node
		if( criteria!=this.#shown ){
			this.#shown = undefined;
			this.grants.reset( [] );
			this.isLoading.set( true );
		}
		try{
			const rows = await this.appService.queryArray<RolePermissionRow>( rolePermissionsQuery, {...this.#resourceKey(), criteria} );
			if( !this.#reads.is(read) )
				return;
			this.grants.reset( toRolePermissions(rows, criteria) );//a permission only where one exists for this exact node
			this.#shown = criteria;
		}
		catch( e ){
			if( !this.#reads.is(read) )
				return;
			this.#shown = undefined;
			this.grants.reset( [] );
			this.snackbar.exception( "Could not load node access.", e );
		}
		finally{
			if( this.#reads.is(read) )
				this.isLoading.set( false );
		}
	}

	async save(){
		if( !this.grants.mutations().length )
			return;
		try{
			await this.grants.send( this.appService );
		}
		catch( e ){
			this.snackbar.exception( "Could not save node access.", e );//a failed save used to be indistinguishable from a successful one - the rejection just escaped
			return;
		}
		await this.load();
	}
	onToggle( event:{ role: RolePermission, rights:number } ):void{ this.grants.toggle( event.role, event.rights ); }
	//schemaName, not `schema:` - the server ignored that and stored schemaName as "opc", putting the permission on the wrong resource
	#resourceKey(){ return { schemaName: `opc.${this.accessResource()}`, slug: "nodeIds" }; }

	access = Rights;//node ACLs use the generic Right enum (Create/Read/Update/Delete/Purge/Administer/Subscribe/Execute), same as the rest of access — NOT EAccess/EWriteAccess
	grants = new RoleGrants( role=>({ ...(role.resourceId ? {id: role.resourceId} : this.#resourceKey()), criteria: role.criteria ? role.criteria : null }) );
	isLoading = signal( true );
	#reads = new Latest();
	#shown:string|undefined;//the criteria of the node whose rows are on screen, from a read that landed
	node = model.required<UaNode>();
	tabIndex = persistedTabIndex( 'nodeAccess' );

	accessResource = input.required<string>();
	appService = inject(AppService);
	snackbar = inject(SnackbarService);
}