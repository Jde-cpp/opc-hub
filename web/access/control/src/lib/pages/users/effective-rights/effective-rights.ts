import { CommonModule } from "@angular/common";
import { Component, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatIconButton } from "@angular/material/button";
import { MatCheckbox } from "@angular/material/checkbox";
import { MatIcon } from "@angular/material/icon";
import { MatTableModule } from "@angular/material/table";
import { MatTooltip } from "@angular/material/tooltip";
import { RouterLink } from "@angular/router";
import { EnumKeysPipe, SnackbarService } from "jde-framework";
import { Rights } from "../../../model/permission";
import { EffectiveRight, PathLists } from "../../../model/effective-right";
import { NODE_LINK_RESOLVER, NodeLink } from "../../../model/node-link";
import { ACCESS_SERVICE, AccessService } from "../../../services/access-service";

//The read-only twin of PermissionTable:  what the user ends up with on each resource through every path - the numbers the
//server enforces (userRights, Authorize::UserRights), not a client-side guess.  Provenance sits on the marks, not in a column
//and not on the Resource cell:  hover a mark for the grants behind that one right, since the paths differ right by right.  Rows are the resources with a
//grant plus every enforced resource without one - that is a lockout, the thing a first-run admin is looking for; an
//unenforced resource with no grant is open to everyone and says nothing.
//Node-scoped rows fold under their table row (EffectiveRight.fromRows nests them), collapsed by default:  every grant made
//from a node's Access tab mints one, so a maintenance user can hold dozens, and the table-level story is what the page is
//for.  The root row carries the toggle and a count; a child row shows only its node, indented - the node's name linking to
//its page when the app can place it (NODE_LINK_RESOLVER, resolved when the root is first expanded), else the criteria as text.
//On a group's page (`{groupId}` in place of `{userId}`) the rows are what a member gets through the group (groupRights,
//Authorize::GroupRights) - each path relative to the group - and there are no lockout rows:  the members may hold elsewhere
//what the group does not give.
//Whose rights:  one required input naming the kind with the id, so strict templates refuse a host that binds neither, both,
//or an id without its kind (reviews-issue/252-effective-rights-group.md #8).
export type RightsHolder = { userId:number; groupId?:never } | { groupId:number; userId?:never };
@Component({
	selector: 'effective-rights',
	templateUrl: 'effective-rights.html',
	styleUrls: ['effective-rights.scss'],
	imports: [CommonModule, MatTableModule, MatCheckbox, MatIcon, MatIconButton, MatTooltip, RouterLink]
})
export class EffectiveRights{
	private accessService:AccessService = inject( ACCESS_SERVICE );
	private snackbar:SnackbarService = inject( SnackbarService );
	private nodeLinks = inject( NODE_LINK_RESOLVER, {optional: true} );

	constructor(){
		effect( ()=>{
			const holder = this.holder(), deleted = this.deleted();
			untracked( ()=>this.#load(holder, deleted) );
		});
	}

	async #load( holder:RightsHolder, deleted:boolean ):Promise<void>{
		const load = ++this.#loadId;//also drops a load still in flight
		if( deleted )
			return;//the server answers nothing for it - the template says why
		this.isLoading.set( true );
		this.expanded.set( new Set() );//the router reuses the page for a sibling record - its roots open closed, so toggle resolves their links
		try{
			const answer = holder.groupId!==undefined ? await this.accessService.groupRights( holder.groupId, this.#lists ) : await this.accessService.effectiveRights( holder.userId, this.#lists );
			this.#lists = answer.lists;
			if( load!=this.#loadId )
				return;//a newer user or group overtook this one
			this.rows.set( answer.rights );
			this.isLoading.set( false );
		}
		catch( e ){
			if( load==this.#loadId )
				this.snackbar.exception( "Could not load the effective rights.", e );//the table stays hidden - a blank grid would read as "no rights"
		}
	}

	isExpanded( row:EffectiveRight ):boolean{ return this.expanded().has( row.resource.id ); }
	toggle( row:EffectiveRight ):void{
		const opening = !this.isExpanded( row );
		this.expanded.update( ids=>{ const y = new Set( ids ); y.has( row.resource.id ) ? y.delete( row.resource.id ) : y.add( row.resource.id ); return y; } );
		if( opening )
			row.children.forEach( child=>this.#resolveLink(child) );
	}
	link( row:EffectiveRight ):NodeLink|undefined{ return this.links().get( row.resource.id ) ?? undefined; }
	linkTitle( link:NodeLink, row:EffectiveRight ):string{ return link.path ? `${link.path} - ${row.resource.criteria}` : row.resource.criteria; }
	//once per child; a node the app cannot place stays text (null), and so does one whose lookup failed - the marks still explain the grant.
	async #resolveLink( row:EffectiveRight ):Promise<void>{
		if( !this.nodeLinks || !row.resource.criteria || this.links().has(row.resource.id) )
			return;
		this.links.update( m=>new Map(m).set(row.resource.id, null) );
		let link:NodeLink|null = null;
		try{
			link = await this.nodeLinks.resolve( row.resource.schema, row.resource.criteria ) ?? null;
		}
		catch( e ){
			console.warn( `effective rights: could not place node '${row.resource.criteria}' of '${row.resource.schema}'.`, e );
		}
		if( link )
			this.links.update( m=>new Map(m).set(row.resource.id, link) );
	}
	childrenLabel( row:EffectiveRight ):string{ return `${row.children.length} ${row.children.length==1 ? "node" : "nodes"}`; }
	isEffective( row:EffectiveRight, right:Rights ):boolean{ return (row.effective & right)!=0; }
	isDenied( row:EffectiveRight, right:Rights ):boolean{ return (row.denied & right)!=0; }
	isAvailable( row:EffectiveRight, right:Rights ):boolean{ return (row.resource.availableRights & right)!=0; }
	tooltip( row:EffectiveRight, right:Rights ):string{ return EffectiveRight.tooltip( row, right ); }

	holder = input.required<RightsHolder>();
	deleted = input<boolean>( false );//the host's row is deleted:  the note in place of a query - reviews-issue/252-effective-rights-group.md #15
	isGroup = computed<boolean>( ()=>this.holder().groupId!==undefined );
	rows = signal<EffectiveRight[]>( [] );//the roots, children nested
	expanded = signal<Set<number>>( new Set() );//resource ids of the roots showing their children
	links = signal<Map<number, NodeLink|null>>( new Map() );//child resource id -> its node page, null while unresolved or unplaceable
	displayRows = computed<EffectiveRight[]>( ()=>this.rows().flatMap( r=>this.expanded().has(r.resource.id) ? [r, ...r.children] : [r] ) );
	isLoading = signal<boolean>( true );
	#loadId = 0;
	#lists:PathLists|undefined;//kept across the sidenav's records - reviews-issue/252-effective-rights-group.md #12
	//the editable grid's None (no rights) and All shortcuts mean nothing as read-only columns
	readonly rightColumns = EnumKeysPipe.prototype.transform( Rights ).filter( kv=>kv.key!=Rights.None && kv.key!=Rights.All );
	readonly displayedColumns = [ "schema", "resource", "enforced", ...this.rightColumns.map( kv=>kv.value ) ];
}
