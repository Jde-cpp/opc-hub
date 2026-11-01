import { Component, computed, model, OnInit, signal, inject } from '@angular/core';
import { MatTabsModule } from '@angular/material/tabs';
import { NodePageData } from '../../services/resolvers/node-resolver';
import { ActivatedRoute } from '@angular/router';
import { ComponentPageTitle } from 'jde-spa';
import { NodeRoute } from '../../model/node-route';
import { NodeChildren } from './node-children/node-children';
import { UaNode, Variable } from '../../model/node';
import { NodeAccess } from './node-access/node-access';
import { NodeHistory } from './node-history/node-history';
import { persistedTabIndex } from 'jde-spa';
import { HistGroupMembers } from '../../model/hist';

@Component( {
	templateUrl: './node-detail.html',
	host: {class:'main-content my-content'},
	imports: [MatTabsModule, NodeChildren, NodeAccess, NodeHistory]
})
export class NodeDetail implements OnInit{
	#activatedRoute = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );
	ngOnInit(){
		this.#activatedRoute.data.subscribe( (data)=>{
			this.pageData.set( data["pageData"] );
			this.nodes.set( this.pageData().nodes );
			this.sideNav.set( this.pageData().route );
			this.#componentPageTitle.title = this.server().connection.name + (this.node().id==85 ? '' : `/${this.node().name}`);
		});
	}

	node = computed( ()=>this.sideNav().node );
	pageData = signal<NodePageData>( null as any );
	nodes = signal<UaNode[]>( [] );//the page's, then each Children refresh's
	//the History tab shows when a child is historized and this user may read its history - a Variable has no
	//page of its own (NodeResolver.#showParent), so the history of its parent's children is where a node's history is seen
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