import { Component, computed, Inject, model, OnDestroy, OnInit, signal, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatTabsModule } from '@angular/material/tabs';
import { NodePageData } from '../../services/resolvers/node-resolver';
import { ActivatedRoute } from '@angular/router';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { NodeRoute } from '../../model/node-route';
import { NodeChildren } from './node-children/node-children';
import { UaNode, Variable } from '../../model/node';
import { NodeAccess } from './node-access/node-access';
import { NodeHistory } from './node-history/node-history';
import { ProfileStore } from 'jde-spa';

@Component( {
	templateUrl: './node-detail.html',
	styleUrls: ['./node-detail.scss'],
	host: {class:'main-content mat-drawer-container my-content'},
	imports: [CommonModule, MatTabsModule, NodeChildren, NodeAccess, NodeHistory]
})
export class NodeDetail implements OnDestroy, OnInit{
	private activatedRoute:ActivatedRoute = inject( ActivatedRoute );
	private componentPageTitle:ComponentPageTitle = inject( ComponentPageTitle );
	ngOnDestroy(){
		ProfileStore.setTabIndex( `nodeDetail/${JSON.stringify(this.node().toJson())}`, this.tabIndex );
  }
	ngOnInit(){
		this.activatedRoute.data.subscribe( (data)=>{
			this.pageData.set( data["pageData"] );
			this.sideNav.set( this.pageData().route );
			this.componentPageTitle.title = this.server().connection.name + (this.node().id==85 ? '' : `/${this.node().name}`);
		});
	}

	node = computed( ()=>this.sideNav().node );
	pageData = signal<NodePageData>( null as any );
	//the History tab shows when a child is historized and this user may read its history (historian 4A #218) - a Variable has no
	//page of its own (NodeResolver.#showParent), so the history of its parent's children is where a node's history is seen
	historizable = computed<Variable[]>( ()=>(<Variable[]>(this.pageData()?.nodes ?? []).filter( n=>n.isVariable )).filter( v=>v.historyReadable ) );
	//get profile(){ return this.pageData().route.profile; }
	server = computed( ()=>this.pageData().server );
	sideNav = model.required<NodeRoute>();
	tabIndex:number=0;
	onTabIndexChanged( index:number ){ this.tabIndex = index; }
}
