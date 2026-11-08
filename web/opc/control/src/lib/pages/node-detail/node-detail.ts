import { Component, computed, effect, model, OnInit, signal, inject, untracked } from '@angular/core';
import { MatTabsModule } from '@angular/material/tabs';
import { NodePageData } from '../../services/resolvers/node-resolver';
import { ActivatedRoute } from '@angular/router';
import { ComponentPageTitle } from 'jde-spa';
import { NodeRoute } from '../../model/node-route';
import { NodeChildren } from './node-children/node-children';
import { UaNode, Variable } from '../../model/node';
import { NodeAccess } from './node-access/node-access';
import { NodeHistory } from './node-history/node-history';
import { NodeReferences } from './node-references/node-references';
import { NodeAttributes } from './node-attributes/node-attributes';
import { NodeEvents } from './node-events/node-events';
import { persistedTabIndex } from 'jde-spa';
import { HistGroupMembers } from '../../model/hist';
import { OpcStore } from '../../services/opc-store';

@Component( {
	templateUrl: './node-detail.html',
	host: {class:'main-content my-content'},
	imports: [MatTabsModule, NodeChildren, NodeAccess, NodeAttributes, NodeEvents, NodeHistory, NodeReferences]
})
export class NodeDetail implements OnInit{
	#activatedRoute = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );
	#opcStore = inject( OpcStore );
	constructor(){
		//A System nodes flip sets the route's siblings again (OpcStore.setShowSystem), on the same object - and ComponentNav, reading a
		//RouteItem's plain fields, repaints only for a new input.  So the sidenav gets a copy, and follows the flip at once
		//(reviews-issue/416-address-space.md #5, #15;  the browser walk found it still waiting for the next navigation).
		let shown:boolean|undefined;
		effect( ()=>{
			const show = this.#opcStore.showSystem();
			untracked( ()=>{
				if( shown!==undefined && shown!==show )
					this.#repaint();
				shown = show;
			} );
		} );
	}
	ngOnInit(){
		this.#activatedRoute.data.subscribe( (data)=>{
			this.pageData.set( data["pageData"] );
			this.nodes.set( this.pageData().nodes );
			this.sideNav.set( this.pageData().route );
			this.#setTitle();
		});
	}
	//A DisplayName written on the Attributes tab (reviews-issue/417-clients-attributes.md #7):  the stored node takes it, so the title and
	//the sidenav's siblings show it at once.  The url is the browse name's, so it stands.
	rename( name:string ){
		this.node().name = name;
		this.#opcStore.refreshRoute();
		this.#repaint();
		this.#setTitle();
	}
	#setTitle(){ this.#componentPageTitle.title = this.server().connection.name + (this.node().isRoot ? '' : `/${this.node().name}`); }
	//the sidenav a copy of the page's route, whose siblings were set again in place:  ComponentNav repaints only for a new input
	#repaint(){
		const route = this.pageData()?.route;
		if( route )
			this.sideNav.set( Object.assign(Object.create(Object.getPrototypeOf(route)), route) );
	}

	node = computed( ()=>this.sideNav().node );
	pageData = signal<NodePageData>( null as any );
	nodes = signal<UaNode[]>( [] );//the page's, then each Children refresh's
	//the History tab shows when a child is historized and this user may read its history - on a Variable's page the variable
	//itself is the first row (NodeResolver), so its own history shows there
	historizable = computed<Variable[]>( ()=>(<Variable[]>this.nodes().filter( n=>n.isVariable )).filter( v=>v.historyReadable ) );
	//the gateway's groups that hold any of the page's variables (NodePageData.groups), each with its members among them:  the
	//History tab's sources beside the server's own, whatever the server historizes
	groups = computed<HistGroupMembers[]>( ()=>{
		const variables = <Variable[]>this.nodes().filter( n=>n.isVariable );
		return ( this.pageData().groups ?? [] ).map( g=>({ id: g.id, name: g.name, nodes: variables.filter( v=>g.nodes.some( n=>n.key==v.key ) ) }) ).filter( g=>g.nodes.length );
	} );
	//get profile(){ return this.pageData().route.profile; }
	server = computed( ()=>this.pageData().server );
	sideNav = model.required<NodeRoute>();
	tabIndex = persistedTabIndex( 'nodeDetail' );//one for every node, as the router reuses the page across them:  the per-node key it replaced was written and never read
}